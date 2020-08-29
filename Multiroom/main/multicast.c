#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "config.h"
#include "multicast.h"
#include "MQTT.h"
#include "audiohome_adf.h"

#define MULTICAST_TTL AUDIOHOME_MULTICAST_TTL
#define MULTICAST_LOOPBACK 'y'

static const char *TAG = "multicast";
static const char *TAG_SEND = "multicast_send";
static const char *TAG_REC = "multicast_receive";

static int sendSocket;
TaskHandle_t sendTask;

static int receiveSocket = 0;
TaskHandle_t receiveTask = NULL;

bool sending = false;
bool stopReceiving = false;

int receivePort = 0;
char receiveAddr[20] = {0};

/* Add a socket, either IPV4-only or IPV6 dual mode, to the IPV4
   multicast group */
static int socket_add_ipv4_multicast_group(int sock, char* addr)
{
    struct ip_mreq imreq = { 0 };
    struct in_addr iaddr = { 0 };
    int err = 0;
    // Configure source interface
    imreq.imr_interface.s_addr = IPADDR_ANY;

    // Configure multicast address to listen to
    err = inet_aton(addr, &imreq.imr_multiaddr.s_addr);
    if (err != 1) {
        ESP_LOGE(TAG, "Configured IPV4 multicast address '%s' is invalid.", addr);
        // Errors in the return value have to be negative
        err = -1;
        goto err;
    }
    ESP_LOGI(TAG, "Configured IPV4 Multicast address %s", inet_ntoa(imreq.imr_multiaddr.s_addr));
    if (!IP_MULTICAST(ntohl(imreq.imr_multiaddr.s_addr))) {
        ESP_LOGW(TAG, "Configured IPV4 multicast address '%s' is not a valid multicast address. This will probably not work.", addr);
    }

    // Assign the IPv4 multicast source interface, via its IP
    // (only necessary if this socket is IPV4 only)
    err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &iaddr,
                        sizeof(struct in_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to set IP_MULTICAST_IF. Error %d", errno);
        goto err;
    }

    err = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                         &imreq, sizeof(struct ip_mreq));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to set IP_ADD_MEMBERSHIP. Error %d", errno);
        goto err;
    }

 err:
    return err;
}
int create_multicast_ipv4_socket(char* addr, int port, bool receive)
{
    struct sockaddr_in saddr = { 0 };
    int sock = -1;
    int err = 0;

    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket. Error %d", errno);
        return -1;
    }

    // Bind the socket to any address
    saddr.sin_family = PF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    err = bind(sock, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to bind socket. Error %d", errno);
        goto err;
    }


    // Assign multicast TTL (set separately from normal interface TTL)
    uint8_t ttl = MULTICAST_TTL;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(uint8_t));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to set IP_MULTICAST_TTL. Error %d", errno);
        goto err;
    }

#if MULTICAST_LOOPBACK
    // select whether multicast traffic should be received by this device, too
    // (if setsockopt() is not called, the default is no)
    uint8_t loopback_val = MULTICAST_LOOPBACK;
    err = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP,
                     &loopback_val, sizeof(uint8_t));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to set IP_MULTICAST_LOOP. Error %d", errno);
        goto err;
    }
#endif

    if (sendSocket) {
        // this is also a listening socket, so add it to the multicast
        // group for listening...
        err = socket_add_ipv4_multicast_group(sock, addr);
        if (err < 0) {
            goto err;
        }
    }

    // All set, socket is configured for sending and receiving
    return sock;

err:
    close(sock);
    return -1;
}

void audio_receive(int sock) {
    while (!stopReceiving) {
        if (sock < 0) {
            ESP_LOGE(TAG_REC, "No Valied Socket for receive");
            return;
        }

        // Loop for Receiving
        int err = 1;
        while (!stopReceiving && err > 0) {
            struct timeval tv = {
                .tv_sec = 2,
                .tv_usec = 0,
            };
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);

            int s = select(sock + 1, &rfds, NULL, NULL, &tv);
            if (s < 0) {
                ESP_LOGE(TAG_REC, "Select failed: errno %d", errno);
                err = -1;
                break;
            }
            else if (s > 0) {
                if (FD_ISSET(sock, &rfds)) {
                    // Incoming datagram received

                    struct sockaddr_in6 raddr; // Large enough for both IPv4 or IPv6
                    socklen_t socklen = sizeof(raddr);
                    receivebuffer_datalen = recvfrom(sock, receivebuffer, sizeof(receivebuffer), 0,
                                       (struct sockaddr *)&raddr, &socklen);
                    if (receivebuffer_datalen < 0) {
                        ESP_LOGE(TAG, "multicast recvfrom failed: errno %d", errno);
                        err = -1;
                        break;
                    }

                    #if AUDIOHOME_DEBUG
                    // For Debig: Get the sender's address as a string
                    char raddr_name[32] = { 0 };
                    if (raddr.sin6_family == PF_INET) {
                        inet_ntoa_r(((struct sockaddr_in *)&raddr)->sin_addr.s_addr,
                                    raddr_name, sizeof(raddr_name)-1);
                    }
                    ESP_LOGI(TAG_REC, "received %d bytes from %s:", receivebuffer_datalen, raddr_name);
                    #endif

                    write_receive_buffer();
                }
            }
            else {
                ESP_LOGI(TAG_REC, "No Data received");
            }
        }
    }

    //warte auf kill
    stopReceiving = false;
    vTaskSuspend( receiveTask );
}
void audio_receive_task(void *pvParameters) {
    receiveTask = xTaskGetCurrentTaskHandle();

    int sock = (int)pvParameters;
    audio_receive(sock);
}

void audio_send(int sock) {
    while(1) { // Forever
        while (sending) { // Keep sending after error
            if (sock < 0) {
                ESP_LOGE(TAG, "No Valied Socket for send");
                return;
            }

            // Prepair Send Resources
            struct addrinfo hints = {
                .ai_flags = AI_PASSIVE,
                .ai_socktype = SOCK_DGRAM,
            };
            struct addrinfo *res;

            hints.ai_family = AF_INET; // For an IPv4 socket
            int err = getaddrinfo(AUDIOHOME_MULTICAST_IPV4_ADDR,
                                    NULL,
                                    &hints,
                                    &res);
            if (err < 0) {
                ESP_LOGE(TAG_SEND, "getaddrinfo() failed for IPV4 destination address. error: %d", err);
                break;
            }
            if (res == 0) {
                ESP_LOGE(TAG_SEND, "getaddrinfo() did not return any addresses");
                #if AUDIOHOME_RESTART_AFTER_NETWORK_COLLAPSE
                return; // force FreeRTOS to restart Device
                #else
                break;
                #endif
            }

            ((struct sockaddr_in *)res->ai_addr)->sin_port = htons(AUDIOHOME_UDP_PORT);

            // Loop for sending
            err = 1;
            while (sending && err > 0) { // Sending Data
                { // s == 0
                    read_send_buffer();

                    #if AUDIOHOME_DEBUG
                    //Debugausgabe
                    char addrbuf[64];
                    inet_ntoa_r(((struct sockaddr_in *)res->ai_addr)->sin_addr, addrbuf, sizeof(addrbuf)-1);
                    ESP_LOGI(TAG_SEND, "Sending to IPV4 multicast address %s:%d...",  addrbuf, AUDIOHOME_UDP_PORT);
                    #endif

                    err = sendto(sock, sendbuffer, sendbuffer_datalen, 0, res->ai_addr, res->ai_addrlen);
                    //freeaddrinfo(res);
                    if (err < 0) {
                        ESP_LOGE(TAG_SEND, "IPV4 sendto failed. errno: %d", errno);
                        break;
                    }

                }
            }
        }

        // Wait for next send
        vTaskSuspend( sendTask );
    }
}
void audio_send_task(void *pvParameters) {
    sendTask = xTaskGetCurrentTaskHandle();

    int sock = (int)pvParameters;
    audio_send(sock);
}

void multicast_init() {
    sendSocket = create_multicast_ipv4_socket(AUDIOHOME_MULTICAST_IPV4_ADDR, AUDIOHOME_LOCAL_SEND_PORT, false);
    xTaskCreate(&audio_send_task, "sendTask", 4096, sendSocket, 5, NULL);
    //sendTask = xTaskGetHandle( "sendTask" );
    ESP_LOGI(TAG, "sendTask startet, socket: %d", sendSocket);
}

void multicast_receive_from_task(void *stop) {
    if (stop) {
        ESP_LOGI(TAG, "stop receiving");
    }
    else {
        ESP_LOGI(TAG, "change to Addr %s on Port %d", receiveAddr, receivePort);
    }

    //delete old
    if (receiveSocket) {
        if (receiveTask) {
            // end recieve
            stopReceiving = true;
            
            // wait receiving to stop
            while (stopReceiving) {
                // do something, because empty loop cause Watchdog-Timeout
                // simple operations cause Timeout too
                ESP_LOGI(TAG, "wait for stop");
            }

            // delete task
            vTaskDelete(receiveTask);
            receiveTask = NULL;
        }

        // free socket
        shutdown(receiveSocket, 0);
        close(receiveSocket);
        receiveSocket = 0;
    }

    bool t = stop;
    ESP_LOGI(TAG, "receiveTask: new task: %d", t);
    if (!stop) {
        // get Socket
        receiveSocket = create_multicast_ipv4_socket(receiveAddr, receivePort, true);

        // start
        xTaskCreate(&audio_receive_task, "receiveTask", 4096, receiveSocket, 5, NULL);
        ESP_LOGI(TAG, "receiveTask started, socket: %d", receiveSocket);
    }

    // kill task
    vTaskDelete(xTaskGetCurrentTaskHandle());
    #if AUDIOHOME_DEBUG
        ESP_LOGI(TAG, "changeTask wait for suicide");
    #endif
    vTaskSuspend(xTaskGetCurrentTaskHandle());
}
void multicast_receive_from(char* addr, int port) {
    ESP_LOGI(TAG, "change aufruf");
    // change?
    if (strcmp(receiveAddr, addr) == 0 && port == receivePort) {
        return;
    }

    if (port == AUDIOHOME_LOCAL_SEND_PORT) { 
        // cann't receive on sendport
        ESP_LOGE(TAG, "cann't receive on sendport");
        return;
    }

    //set variables
    receivePort = port;
    strncpy(receiveAddr, addr, 19);

    //start Task to Change
    ESP_LOGI(TAG, "start changeTask");
    xTaskCreate(&multicast_receive_from_task, "changeTask", 4096, false, 5, NULL);
}

void multicast_stop_receiving() {
    receivePort = 0;
    xTaskCreate(&multicast_receive_from_task, "stopReceiveTask", 4096, true, 5, NULL);
}

void multicast_start_sending() {
    if (!sending) {
        sending = true;

        //start Transfer
        vTaskResume( sendTask );
        ESP_LOGI(TAG, "start transfer");
        mqtt_log("Sendestatus: start transfer");
    }
}
void multicast_stop_sending() {
    if (sending) {
        sending = false;

        ESP_LOGI(TAG, "stop transfer");
        mqtt_log("Sendestatus: stop transfer");
    }
}
bool multicast_toogle_sending() {
    if (sending) {
        multicast_stop_sending();
        return false;
    }
    else {
        multicast_start_sending();
        return true;
    }
}

