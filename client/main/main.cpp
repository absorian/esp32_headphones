/* Play music from Bluetooth device

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#define BTM_SCO_DATA_SIZE_MAX 240

#include <cinttypes>
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"

#include "hf_handle.h"

//#include "impl.h"

#include "esp_log.h"
#include "esp_peripherals.h"
#include "esp_bt_defs.h"
#include "esp_gap_bt_api.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"

#include "i2s_stream.h"
#include "bluetooth_service.h"
#include "raw_stream.h"


static const char *TAG = "BLUETOOTH_EXAMPLE";

static esp_periph_handle_t bt_periph = NULL;

audio_element_handle_t bt_stream_reader, raw_read;
static audio_element_handle_t i2s_stream_writer, i2s_stream_reader;
static audio_pipeline_handle_t pipeline_d, pipeline_e;

const i2s_pin_config_t pin_config_spk = {
        .bck_io_num = 14,
        .ws_io_num = 27,
        .data_out_num = 22,
        .data_in_num = I2S_PIN_NO_CHANGE
};
const i2s_pin_config_t pin_config_mic = {
        .bck_io_num = 16, // 4
        .ws_io_num = 15,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = 4 // 2
};

#define NUM_CHANNELS_SPK 2
#define NUM_CHANNELS_MIC 1

#define USE_FLAC_SPK 0
#define SAMPLE_RATE 44100
const ip_address_t HOST_ADDR = ip_address_t::from_string("192.168.1.4");
#define PORT 533

#include "controller.h"
#include "sender.h"
#include "receiver.h"

#define PIPE_WIDTH 1024 - (controller_t::md_size() / 4 + (controller_t::md_size() % 4 != 0)) * 4

#if NUM_CHANNELS_SPK == 1
#define CHANNEL_FMT_SPK I2S_CHANNEL_FMT_ONLY_RIGHT
#else
#define CHANNEL_FMT_SPK I2S_CHANNEL_FMT_RIGHT_LEFT
#endif

#if NUM_CHANNELS_MIC == 1
#define CHANNEL_FMT_MIC I2S_CHANNEL_FMT_ONLY_LEFT
#else
#define CHANNEL_FMT_MIC I2S_CHANNEL_FMT_RIGHT_LEFT
#endif

#if USE_FLAC_SPK

#include <flac_decoder.h>

#endif

#define DMA_BUF_COUNT 3
#define DMA_BUF_SIZE 1024

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static int volume_convert_alc(int vol) {
    return vol ? map(vol, 0, 127, -40, 0) : -64;
}

static int default_volume = 64; // 0..127
static void avrc_tg_callback(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param) {
    ESP_LOGD(TAG, "%s evt %d", __func__, event);
    esp_avrc_tg_cb_param_t *rc = (esp_avrc_tg_cb_param_t *)(param);
    switch (event) {
        case ESP_AVRC_TG_CONNECTION_STATE_EVT: {
            uint8_t *bda = rc->conn_stat.remote_bda;
            ESP_LOGI(TAG, "AVRC conn_state evt: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                     rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            break;
        }
        case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT: {
            ESP_LOGI(TAG, "AVRC passthrough cmd: key_code 0x%x, key_state %d", rc->psth_cmd.key_code, rc->psth_cmd.key_state);
            break;
        }
        case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT: {
            ESP_LOGI(TAG, "AVRC set absolute volume: %d%%", (int)rc->set_abs_vol.volume * 100 / 0x7f);
            i2s_alc_volume_set(i2s_stream_writer, volume_convert_alc(rc->set_abs_vol.volume));
            default_volume = rc->set_abs_vol.volume;
            break;
        }
        case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT: {
            ESP_LOGI(TAG, "AVRC register event notification: %d, param: 0x%lx", rc->reg_ntf.event_id, rc->reg_ntf.event_parameter);
            if (rc->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
                esp_avrc_rn_param_t rn_param;
                rn_param.volume = default_volume;
                ESP_LOGI(TAG, "rn_param.volume:%d", rn_param.volume);
                esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &rn_param);
            }
            break;
        }
        case ESP_AVRC_TG_REMOTE_FEATURES_EVT: {
            ESP_LOGW(TAG, "AVRC tg remote features %lx, CT features %x", rc->rmt_feats.feat_mask, rc->rmt_feats.ct_feat_flag);
            break;
        }
        default:
            ESP_LOGE(TAG, "%s unhandled evt %d", __func__, event);
            break;
    }
}

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "[ 1 ] Create Bluetooth service");
    bluetooth_service_cfg_t bt_cfg = {
            .device_name = "ESP-ADF-AUDIO",
            .mode = BLUETOOTH_A2DP_SINK,
    };
    bluetooth_service_start(&bt_cfg);

    esp_avrc_tg_init();
    esp_avrc_tg_register_callback(avrc_tg_callback);
    esp_avrc_rn_evt_cap_mask_t evt_set = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
    esp_avrc_tg_set_rn_evt_cap(&evt_set);

    esp_hf_client_register_callback(bt_hf_client_cb);
    esp_hf_client_init();

    ESP_LOGI(TAG, "[ 3 ] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_d = audio_pipeline_init(&pipeline_cfg);
    pipeline_e = audio_pipeline_init(&pipeline_cfg);

    ESP_LOGI(TAG, "[3.1] Create i2s stream to write data to codec chip and read data from codec chip");
    i2s_stream_cfg_t i2s_spk_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_spk_cfg.i2s_port = I2S_NUM_0;
    i2s_spk_cfg.type = AUDIO_STREAM_WRITER;
    i2s_spk_cfg.i2s_config.mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_spk_cfg.i2s_config.sample_rate = SAMPLE_RATE;
    i2s_spk_cfg.i2s_config.channel_format = CHANNEL_FMT_SPK;
    i2s_spk_cfg.i2s_config.use_apll = true;
//    i2s_spk_cfg.i2s_config.dma_desc_num = DMA_BUF_COUNT;
//    i2s_spk_cfg.i2s_config.dma_frame_num = DMA_BUF_SIZE;
    i2s_spk_cfg.i2s_config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    i2s_spk_cfg.use_alc = true;
    i2s_spk_cfg.volume = volume_convert_alc(default_volume);

    i2s_stream_writer = i2s_stream_init(&i2s_spk_cfg);
    i2s_set_pin(i2s_spk_cfg.i2s_port, &pin_config_spk);

    i2s_stream_cfg_t i2s_mic_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_mic_cfg.i2s_port = I2S_NUM_1;
    i2s_mic_cfg.type = AUDIO_STREAM_READER;
    i2s_mic_cfg.i2s_config.mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_RX);
    i2s_mic_cfg.i2s_config.sample_rate = HFP_RESAMPLE_RATE;
    i2s_mic_cfg.i2s_config.bits_per_sample = i2s_bits_per_sample_t(16);
    i2s_mic_cfg.i2s_config.channel_format = CHANNEL_FMT_MIC;
    i2s_mic_cfg.i2s_config.use_apll = false;
    i2s_mic_cfg.i2s_config.dma_desc_num = DMA_BUF_COUNT;
    i2s_mic_cfg.i2s_config.dma_frame_num = DMA_BUF_SIZE;
    i2s_mic_cfg.i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_mic_cfg.use_alc = true;
    i2s_mic_cfg.volume = 64;

    i2s_stream_reader = i2s_stream_init(&i2s_mic_cfg);
    i2s_set_pin(i2s_mic_cfg.i2s_port, &pin_config_mic);

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    raw_read = raw_stream_init(&raw_cfg);

    ESP_LOGI(TAG, "[3.2] Create Bluetooth stream");
    bt_stream_reader = bluetooth_service_create_stream();

    ESP_LOGI(TAG, "[3.3] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline_d, bt_stream_reader, "bt");
    audio_pipeline_register(pipeline_d, i2s_stream_writer, "i2s_w");

    audio_pipeline_register(pipeline_e, i2s_stream_reader, "i2s_r");
    audio_pipeline_register(pipeline_e, raw_read, "raw");

    ESP_LOGI(TAG, "[3.4] Link it together [Bluetooth]-->bt_stream_reader-->i2s_stream_writer-->[codec_chip]");
    const char *link_d[2] = {"bt", "i2s_w"};
    audio_pipeline_link(pipeline_d, &link_d[0], 2);

    const char *link_e[2] = {"i2s_r", "raw"};
    audio_pipeline_link(pipeline_e, &link_e[0], 2);

    ESP_LOGI(TAG, "[ 4 ] Initialize peripherals");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);


    ESP_LOGI(TAG, "[4.2] Create Bluetooth peripheral");
    bt_periph = bluetooth_service_create_periph();

    ESP_LOGI(TAG, "[4.2] Start all peripherals");
    esp_periph_start(set, bt_periph);

    ESP_LOGI(TAG, "[ 5 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[5.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline_d, evt);

    ESP_LOGI(TAG, "[5.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 6 ] Start audio_pipeline");
    audio_pipeline_run(pipeline_d);
    audio_pipeline_run(pipeline_e);

    ESP_LOGI(TAG, "[ 7 ] Listen for all pipeline events");
    while (1) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) bt_stream_reader
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info;
            audio_element_getinfo(bt_stream_reader, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from Bluetooth, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);
//            audio_element_setinfo(i2s_stream_writer, &music_info);
            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            audio_pipeline_reset_ringbuffer(pipeline_d);
            continue;
        }
//        if ((msg.source_type == PERIPH_ID_TOUCH || msg.source_type == PERIPH_ID_BUTTON || msg.source_type == PERIPH_ID_ADC_BTN)
//            && (msg.cmd == PERIPH_TOUCH_TAP || msg.cmd == PERIPH_BUTTON_PRESSED || msg.cmd == PERIPH_ADC_BUTTON_PRESSED)) {
//
//            if ((int) msg.data == get_input_play_id()) {
//                ESP_LOGI(TAG, "[ * ] [Play] touch tap event");
//                periph_bluetooth_play(bt_periph);
//            } else if ((int) msg.data == get_input_set_id()) {
//                ESP_LOGI(TAG, "[ * ] [Set] touch tap event");
//                periph_bluetooth_pause(bt_periph);
//            } else if ((int) msg.data == get_input_volup_id()) {
//                ESP_LOGI(TAG, "[ * ] [Vol+] touch tap event");
//                periph_bluetooth_next(bt_periph);
//            } else if ((int) msg.data == get_input_voldown_id()) {
//                ESP_LOGI(TAG, "[ * ] [Vol-] touch tap event");
//                periph_bluetooth_prev(bt_periph);
//            }
//        }

        /* Stop when the Bluetooth is disconnected or suspended */
        if (msg.source_type == PERIPH_ID_BLUETOOTH
            && msg.source == (void *)bt_periph) {
            if (msg.cmd == PERIPH_BLUETOOTH_DISCONNECTED) {
                ESP_LOGW(TAG, "[ * ] Bluetooth disconnected");
                break;
            }
        }
        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS && (int) msg.data == AEL_STATUS_STATE_STOPPED) {
            ESP_LOGW(TAG, "[ * ] Stop event received");
            break;
        }
    }

    ESP_LOGI(TAG, "[ 8 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline_d);
    audio_pipeline_wait_for_stop(pipeline_d);
    audio_pipeline_terminate(pipeline_d);
    audio_pipeline_stop(pipeline_e);
    audio_pipeline_wait_for_stop(pipeline_e);
    audio_pipeline_terminate(pipeline_e);

    audio_pipeline_unregister(pipeline_d, bt_stream_reader);
    audio_pipeline_unregister(pipeline_d, i2s_stream_writer);

    audio_pipeline_unregister(pipeline_e, i2s_stream_reader);
    audio_pipeline_unregister(pipeline_e, raw_read);

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline_d);

    /* Stop all peripherals before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline_d);
    audio_element_deinit(bt_stream_reader);
    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(i2s_stream_reader);
    audio_element_deinit(raw_read);

    esp_periph_set_destroy(set);
    bluetooth_service_destroy();

    esp_restart();
}
