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

// ADF
#include "esp_peripherals.h"
#include "board.h"
#include "periph_wifi.h"

#include "multicast.h"
#include "buttons.h"
#include "MQTT.h"
#include "audiohome_adf.h"
#include "config.h"

static const char *TAG = "main";

void app_main()
{

    ESP_LOGI(TAG, "[1.0] Initialize start");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
    tcpip_adapter_init();
    //ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "[1.1] Initialize peripherals management");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "[1.2] Initialize and start peripherals");
    buttons_init(set);

    ESP_LOGI(TAG, "[1.3] Start and wait for Wi-Fi network");
    periph_wifi_cfg_t wifi_cfg = {
        .ssid = AUDIOHOME_WIFI_SSID,
        .password = AUDIOHOME_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);

    ESP_LOGI(TAG, "[ 2 ] Start Multicast");
    multicast_init();

    ESP_LOGI(TAG, "[ 3 ] Start Audio processing");
    start_audio_pipes();

    ESP_LOGI(TAG, "[ 6 ] Start MQTT");
    mqtt_start();

    ESP_LOGI(TAG, "[ 7 ] Finish Startup");
}
