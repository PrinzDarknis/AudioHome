/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "tcpip_adapter.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "MQTT.h"
#include "config.h"
#include "multicast.h"

#define MQTT_LINE_IN_PRE "AudioDevice/Line-In/"
#define MQTT_LINE_OUT_PRE "AudioDevice/Line-Out/"
#define MQTT_ADDRESS_BUFFER_SIZE 100

static const char *TAG = "MQTT";

static char mqtt_address[MQTT_ADDRESS_BUFFER_SIZE];
static char mqtt_plays[MQTT_ADDRESS_BUFFER_SIZE];
static char mqtt_lineout[MQTT_ADDRESS_BUFFER_SIZE];
static char mqtt_log_topic[MQTT_ADDRESS_BUFFER_SIZE];
static char my_address[20];
static char souce_address[20];
static char state[10] = "none";

bool connected = false;
esp_mqtt_client_handle_t client;

static void got_MQTT(char *topic, char *data) {
    if (strcmp(topic, mqtt_address) == 0) {
        // ignore, sollte nicht passieren
    }
    else if (strcmp(topic, mqtt_plays) == 0) {
        strncpy(state, data, 9);
        ESP_LOGI(TAG, "Change State to %s", state);
        
        // Stelle Wiedergabe ein
        if (strcmp(state, "play") == 0) {
            multicast_start_sending();
        }
        else {
            multicast_stop_sending();
        }
    }
    else if (strcmp(topic, mqtt_lineout) == 0) {
        // Änderung?
        if (strcmp(data, souce_address) == 0) {
            #if false //AUDIOHOME_DEBUG
                ESP_LOGI(TAG, "Die Adresse %s wird bereits wiedergegeben", souce_address);
            #endif
            return;
        }

        // neue Adresse speichern
        strncpy(souce_address, data, 19);
        ESP_LOGI(TAG, "Change Souce-Address to %s", souce_address);

        // stop?
        if (strcmp(data, "none") == 0) {
            multicast_stop_receiving();
            mqtt_log("neue Wiedergabe: none");
            return;
        }
        
        // split in address und port
        char addr[20];
        char port_str[20];
        char *ptr, temp[20];
        int port;

        strncpy(temp, souce_address, 19);
        ptr = strtok (temp,":");
        strncpy(addr, ptr, 19);
        ptr = strtok (NULL,":");
        strncpy(port_str, ptr, 19);

        // ist Adresse gültig?
        if (inet_aton(addr, temp) == 0) {
            ESP_LOGE(TAG, "ungültige Adresse: %s", addr);
                return;
        }

        // ist Port gültig?
        for (int i = 0; i < strlen(port_str); i++) {
            if (!isdigit(port_str[i])) {
                ESP_LOGE(TAG, "ungültiger Port: %s", port_str);
                return;
            }
        }
        port = atoi(port_str);

        // Stelle Wiedergabe ein
        multicast_receive_from(addr, port);
        char message[100];
        sprintf(message, "neue Wiedergabe: Addr: %s, Port: %d", addr, port);
        mqtt_log(message);
    }
    else {
        ESP_LOGI(TAG, "Unbekanntes Topic :%s", topic);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    // pars variable
    esp_mqtt_event_handle_t event = event_data;
    //esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    // handle event
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

            //Subscripe
            msg_id = esp_mqtt_client_subscribe(client, mqtt_plays, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, mqtt_lineout, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            
            //Publish Bootdata
            msg_id = esp_mqtt_client_publish(client, mqtt_address, my_address, 0, 1, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

            #if AUDIOHOME_RESET_PLAYS_ON_START
            msg_id = esp_mqtt_client_publish(client, mqtt_plays, state, 0, 1, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            #endif

            msg_id = esp_mqtt_client_publish(client, mqtt_lineout, "none", 0, 1, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

            connected = true;
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGD(TAG, "MQTT_EVENT_DATA");
            char topic[50], data[50];
            sprintf(topic, "%.*s", event->topic_len, event->topic);
            sprintf(data, "%.*s", event->data_len, event->data);
            got_MQTT(topic, data);
            break;

        // more events
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            connected = false;
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

void mqtt_start()
{
    //prepare Strings
    snprintf(mqtt_address, MQTT_ADDRESS_BUFFER_SIZE, "%s%s/Address", MQTT_LINE_IN_PRE, AUDIOHOME_LINE_IN_NAME);
    snprintf(mqtt_plays, MQTT_ADDRESS_BUFFER_SIZE, "%s%s/plays", MQTT_LINE_IN_PRE, AUDIOHOME_LINE_IN_NAME);
    snprintf(mqtt_log_topic, MQTT_ADDRESS_BUFFER_SIZE, "%s%s/log", MQTT_LINE_IN_PRE, AUDIOHOME_LINE_IN_NAME);
    snprintf(mqtt_lineout, MQTT_ADDRESS_BUFFER_SIZE, "%s%s", MQTT_LINE_OUT_PRE, AUDIOHOME_LINE_OUT_NAME);
    snprintf(my_address, 20, "%s:%d", AUDIOHOME_MULTICAST_IPV4_ADDR, AUDIOHOME_UDP_PORT);

    #if AUDIOHOME_DEBUG
    ESP_LOGI(TAG, "Adress : %s", mqtt_address);
    ESP_LOGI(TAG, "plays  : %s", mqtt_plays);
    ESP_LOGI(TAG, "log    : %s", mqtt_log_topic);
    ESP_LOGI(TAG, "MyAddr : %s", my_address);
    #endif

    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = AUDIOHOME_MQTT_SERVER,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}

void mqtt_send_play() {
    if (!connected) {
        return;
    }

    strncpy(state, "play", 9);

    int msg_id = esp_mqtt_client_publish(client, mqtt_plays, state, 0, 1, 0);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
}

void mqtt_send_none_play() {
    if (!connected) {
        return;
    }
    
    strncpy(state, "none", 9);

    int msg_id = esp_mqtt_client_publish(client, mqtt_plays, state, 0, 1, 0);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
}

void mqtt_log(char *message) {
    if (!connected) {
        return;
    }
    
    int msg_id = esp_mqtt_client_publish(client, mqtt_log_topic, message, 0, 1, 0);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
}
