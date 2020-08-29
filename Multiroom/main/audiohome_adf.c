#include "esp_system.h"
#include "esp_log.h"
#include "board.h"

#include "audio_pipeline.h"
#include "i2s_stream.h"
#include "raw_stream.h"
#include "wav_encoder.h"
#include "wav_decoder.h"
#include "filter_resample.h"

#include "config.h"
#include "audiohome_adf.h"

static const char *TAG = "ADF";

#define I2S_SAMPLE_RATE     48000
#define I2S_CHANNELS        2
#define I2S_BITS            16

#define RS_SAMPLE_RATE     48000 //16000
#define RS_CHANNELS        1 //2

// Variable for Share
int sendbuffer_datalen = 0;
char sendbuffer[AUDIOHOME_ADF_BUFFER_SIZE];

int receivebuffer_datalen = 0;
char receivebuffer[AUDIOHOME_ADF_BUFFER_SIZE];

// Intern Variable
static audio_element_handle_t raw_read, raw_write;
static audio_pipeline_handle_t recorder, player;

/* Qualitätstest:
 * 
 * 1. WAV En-/Decode:
 *          - Fehler
 *          - E (21698) DEC_WAV: wav_head_parser Failed (ERR 2)
 *          - E (21698) AUDIO_ELEMENT: [wav] AEL_STATUS_ERROR_OPEN,-1
 * 
 * 2. Resample Filter:
 *          - Max Buffergröße: 29000
 *                  - extrem Stockend
 *                  - Musik nicht als solche erkennbar
 *                  - schlechter als Nur RAW bei selber Buffergröße
 *                              - Nur RAW: Musik erkennbar, identifizierbar, halbwegs höhrbar
 * 
 * 3. Nur RAW:
 *          - Max Buffergröße: 38000
 *                  - flüssig
 *                  - Musik verständlich
 *                  - Hintergrundrauschen
 *                  - Probleme bei lauten tönen
 *                  => Probleme tretten auch bei Paththrough auf
 *                  => warscheinlich Hardwarelimitierungen des Lyra-T
 * 
 * => Bei einer Buffergröße von 38000 können die Pakete nicht mehr im Netzwerk übertragen werden.
 *      - Ab einer größe von ca. 1500 werden die Pakete nicht mehr übertragen
 *      - Bei 1400 kommt es aufgrund der anzahl der Pakete zu speicherproblemen
 *      => Bei einer Paketgröße 1300 werden die Pakete relativ stabil übertragen
 *                  - stockend
 *                  - Musik manchmal indentifizierbar
 *                  => ohne anderes Übertragungsprotokoll nicht nutzbar
 *      - Ursache warscheinlich: Ethernetpaket max. 1500 Bytes payload
 * 
 * => Bei genügend Abstand zwischen den Lyra-Ts ist die Übertragungsqualtiät sehr viel besser
 *                  - flüssig
 *                  - Musik verständlich
 *                  - verstärkt Hintergrundrauschen
 *                  - Probleme bei lauten tönen
 *                  - Einbrüche nach einer Weile
 *                      - Ursache: Sender bekommt einen ErrorCode 12 "Out of memory"
 *                          bei der methode sendto multicast.c Zeile 250-265
 *                          - Tritt auf, wenn zu viele Pakete geschickt werden
 *                          - keine Speicherallocation innerhalb der Schleife sichtbar
 */ 

#define WAV_CODEC false
#define FILTER false

static esp_err_t recorder_pipeline_open() 
{
    ESP_LOGI(TAG, "[4.1] Init Pipe");
    audio_element_handle_t i2s_stream_reader;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    recorder = audio_pipeline_init(&pipeline_cfg);
    AUDIO_NULL_CHECK(TAG, recorder, return ESP_FAIL);

    ESP_LOGI(TAG, "[4.2] Init I2C");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_READER;
    //i2s_cfg.uninstall_drv = false;
#if defined CONFIG_ESP_LYRAT_MINI_V1_1_BOARD
    i2s_cfg.i2s_port = 1;
#endif
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);
    audio_element_info_t i2s_info = {0};
    audio_element_getinfo(i2s_stream_reader, &i2s_info);
    i2s_info.bits = I2S_BITS;
    i2s_info.channels = I2S_CHANNELS;
    i2s_info.sample_rates = I2S_SAMPLE_RATE;
    audio_element_setinfo(i2s_stream_reader, &i2s_info);

#if FILTER
    ESP_LOGI(TAG, "[4.X] Resample Filter");
    rsp_filter_cfg_t rsp_file_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_file_cfg.src_rate = I2S_SAMPLE_RATE;
    rsp_file_cfg.src_ch = I2S_CHANNELS;
    rsp_file_cfg.dest_rate = RS_SAMPLE_RATE;
    rsp_file_cfg.dest_ch = RS_CHANNELS;
    audio_element_handle_t resample_for_rec = rsp_filter_init(&rsp_file_cfg);
#endif

#if WAV_CODEC
    ESP_LOGI(TAG, "[4.X] WAV Encoder");
    wav_encoder_cfg_t wav_file_cfg = DEFAULT_WAV_ENCODER_CONFIG();
    audio_element_handle_t wav_encoder = wav_encoder_init(&wav_file_cfg);
#endif

    ESP_LOGI(TAG, "[4.3] Init RAW");
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    raw_read = raw_stream_init(&raw_cfg);
    audio_element_set_output_timeout(raw_read, portMAX_DELAY);

    ESP_LOGI(TAG, "[4.4] Register Pipe");
    audio_pipeline_register(recorder, i2s_stream_reader, "i2s");
#if FILTER
    audio_pipeline_register(recorder, resample_for_rec, "filter");
#endif
#if WAV_CODEC
    audio_pipeline_register(recorder, wav_encoder, "wav");
#endif
    audio_pipeline_register(recorder, raw_read, "raw");
    
#if WAV_CODEC && FILTER
    audio_pipeline_link(recorder, (const char *[]) {"i2s", "filter", "wav", "raw"}, 4);
#elif WAV_CODEC
    audio_pipeline_link(recorder, (const char *[]) {"i2s", "wav", "raw"}, 3);
#elif FILTER
    audio_pipeline_link(recorder, (const char *[]) {"i2s", "filter", "raw"}, 3);
#else
    audio_pipeline_link(recorder, (const char *[]) {"i2s", "raw"}, 2);
#endif

    ESP_LOGI(TAG, "[4.5] Start Input Pipe");
    audio_pipeline_run(recorder);
    return ESP_OK;
}

static esp_err_t player_pipeline_open()
{
    ESP_LOGI(TAG, "[5.1] Init Pipe");
    audio_element_handle_t i2s_stream_writer;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    player = audio_pipeline_init(&pipeline_cfg);
    AUDIO_NULL_CHECK(TAG, player, return ESP_FAIL);

    ESP_LOGI(TAG, "[5.2] Init RAW");
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    raw_write = raw_stream_init(&raw_cfg);

#if WAV_CODEC
    ESP_LOGI(TAG, "[5.X] WAV Decoder");
    wav_decoder_cfg_t wav_dec_cfg = DEFAULT_WAV_DECODER_CONFIG();
    audio_element_handle_t wav_decoder = wav_decoder_init(&wav_dec_cfg);
#endif

#if FILTER
    ESP_LOGI(TAG, "[5.X] Resample Filter");
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    audio_element_handle_t resample_for_play = rsp_filter_init(&rsp_cfg);
#endif

    ESP_LOGI(TAG, "[5.3] Init I2C");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.uninstall_drv = false;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    audio_element_info_t i2s_info = {0};
    audio_element_getinfo(i2s_stream_writer, &i2s_info);
    i2s_info.bits = I2S_BITS;
    i2s_info.channels = I2S_CHANNELS;
    i2s_info.sample_rates = I2S_SAMPLE_RATE;
    audio_element_setinfo(i2s_stream_writer, &i2s_info);

    ESP_LOGI(TAG, "[5.4] Register Pipe");
    audio_pipeline_register(player, raw_write, "raw");
#if WAV_CODEC
    audio_pipeline_register(player, wav_decoder, "wav");
#endif
#if FILTER
    audio_pipeline_register(player, resample_for_play, "filter");
#endif
    audio_pipeline_register(player, i2s_stream_writer, "i2s");

#if WAV_CODEC && FILTER
    audio_pipeline_link(player, (const char *[]) {"raw", "wav", "filter", "i2s"}, 4);
#elif WAV_CODEC
    audio_pipeline_link(player, (const char *[]) {"raw", "wav", "i2s"}, 3);
#elif FILTER
    audio_pipeline_link(player, (const char *[]) {"raw", "filter", "i2s"}, 3);
#else
    audio_pipeline_link(player, (const char *[]) {"raw", "i2s"}, 2);
#endif

    ESP_LOGI(TAG, "[5.5] Start Input Pipe");
    audio_pipeline_run(player);
    return ESP_OK;
}


void read_send_buffer() {
    sendbuffer_datalen = raw_stream_read(raw_read, sendbuffer, AUDIOHOME_ADF_BUFFER_SIZE);
}

void write_receive_buffer() {
    raw_stream_write(raw_write, receivebuffer, receivebuffer_datalen);
}

void start_audio_pipes() {
    ESP_LOGI(TAG, "[3.1] Start codec chip");
    audio_hal_codec_config_t audio_hal_codec_cfg = AUDIO_CODEC_DEFAULT_CONFIG();
    audio_hal_codec_cfg.adc_input = AUDIO_HAL_ADC_INPUT_LINE2;  // Set AUX_IN port as audio input

    audio_board_handle_t board_handle = audio_board_init();
    board_handle->audio_hal = audio_hal_init(&audio_hal_codec_cfg, &AUDIO_CODEC_ES8388_DEFAULT_HANDLE);
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG, "[ 4 ] Create Input Pipe");
    recorder_pipeline_open();

    ESP_LOGI(TAG, "[ 5 ] Create Output Pipe");
    player_pipeline_open();
}
