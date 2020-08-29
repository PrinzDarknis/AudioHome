#include "esp_system.h"
#include "esp_log.h"
#include "esp_peripherals.h"
#include "board.h"
#include "input_key_service.h"

#include "buttons.h"
#include "multicast.h"
#include "MQTT.h"
#include "config.h"

static const char *TAG = "buttons";

static bool mode = false;

static esp_err_t input_key_service_callback(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx) {
    audio_board_handle_t board_handle = (audio_board_handle_t) ctx;
    int player_volume;
    audio_hal_get_volume(board_handle->audio_hal, &player_volume);
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
        ESP_LOGD(TAG, "[ * ] input key id is %d", (int)evt->data);
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_REC:
                ESP_LOGI(TAG, "[ * ] [Rec] input key event");
                break;
            case INPUT_KEY_USER_ID_PLAY:
                ESP_LOGI(TAG, "[ * ] [Play] input key event");
                bool newState = multicast_toogle_sending();

                if (newState) {
                    mqtt_send_play();
                }
                else {
                    mqtt_send_none_play();
                }
                break;
            case INPUT_KEY_USER_ID_MODE:
                ESP_LOGI(TAG, "[ * ] [Mode] input key event");
                #if AUDIOHOME_DEBUG
                    if (mode) {
                        multicast_receive_from("232.10.11.1", 3333);
                    }
                    else {
                        multicast_receive_from("232.10.11.2", 3333);
                    }
                    mode = !mode;
                #endif
                break;
            case INPUT_KEY_USER_ID_SET:
                ESP_LOGI(TAG, "[ * ] [Set] input key event");
                break;
            case INPUT_KEY_USER_ID_VOLUP:
                ESP_LOGD(TAG, "[ * ] [Vol+] input key event");
                player_volume += 10;
                if (player_volume > 100) {
                    player_volume = 100;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
                break;
            case INPUT_KEY_USER_ID_VOLDOWN:
                ESP_LOGD(TAG, "[ * ] [Vol-] input key event");
                player_volume -= 10;
                if (player_volume < 0) {
                    player_volume = 0;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
                break;
        }
    }

    return ESP_OK;
}


void buttons_init(esp_periph_set_handle_t set) {
    audio_board_key_init(set);

    // init Key-Bind
    audio_board_handle_t board_handle = audio_board_init();
    input_key_service_info_t input_key_info[] = INPUT_KEY_DEFAULT_INFO();
    input_key_service_cfg_t input_cfg = INPUT_KEY_SERVICE_DEFAULT_CONFIG();
    input_cfg.handle = set;
    periph_service_handle_t input_ser = input_key_service_create(&input_cfg);
    input_key_service_add_key(input_ser, input_key_info, INPUT_KEY_NUM);

    // set Key-Bind
    periph_service_set_callback(input_ser, input_key_service_callback, (void *)board_handle);
}