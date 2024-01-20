/* Play music from Bluetooth device

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#define BTM_SCO_DATA_SIZE_MAX 240

#include <cstddef>
#include <cinttypes>
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"

//#include "impl.h"

#include "esp_log.h"
#include "esp_peripherals.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"

#include "i2s_stream.h"
#include "raw_stream.h"


static const char *TAG = "BLUETOOTH_EXAMPLE";

const i2s_pin_config_t pin_config_spk = {
        .bck_io_num = 13,
        .ws_io_num = 14,
        .data_out_num = 12,
        .data_in_num = I2S_PIN_NO_CHANGE
};
const i2s_pin_config_t pin_config_mic = {
        .bck_io_num = 16,
        .ws_io_num = 15,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = 4
};

#define NUM_CHANNELS_SPK 2
#define NUM_CHANNELS_MIC 1

#define USE_FLAC_SPK 0
#define SAMPLE_RATE 44100
//const ip_address_t HOST_ADDR = ip_address_t::from_string("192.168.1.4");
#define PORT 533

#include "controller.h"
#include "sender.h"
#include "receiver.h"
#include "btstack_impl.h"

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
#define DMA_BUF_SIZE 960 // THIS or initial setup at 16000

static audio_element_handle_t i2s_stream_writer, i2s_stream_reader;
static audio_pipeline_handle_t pipeline_spk, pipeline_mic;

// warn about unsuitable sdkconfig
#include "sdkconfig.h"
#if !CONFIG_BT_ENABLED
#error "Bluetooth disabled - please set CONFIG_BT_ENABLED via menuconfig -> Component Config -> Bluetooth -> [x] Bluetooth"
#endif
#if !CONFIG_BT_CONTROLLER_ONLY
#error "Different Bluetooth Host stack selected - please set CONFIG_BT_CONTROLLER_ONLY via menuconfig -> Component Config -> Bluetooth -> Host -> Disabled"
#endif
#if ESP_IDF_VERSION_MAJOR >= 5
#if !CONFIG_BT_CONTROLLER_ENABLED
#error "Different Bluetooth Host stack selected - please set CONFIG_BT_CONTROLLER_ENABLED via menuconfig -> Component Config -> Bluetooth -> Controller -> Enabled"
#endif
#endif


static int volume_convert_alc(int vol) {
    return vol ? map(vol, 0, 127, -40, 0) : -64;
}


static void evt_handler_loop(void* ctx) {
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    audio_event_iface_set_listener(bt_evt_iface, evt);
    audio_pipeline_set_listener(pipeline_mic, evt);
    audio_pipeline_set_listener(pipeline_spk, evt);

    while(true) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == bt_ael_mic) {
            if (msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info;
                audio_element_getinfo(bt_ael_mic, &music_info);

                ESP_LOGI(TAG, "Set clk for i2s_mic, sample_rates=%d, bits=%d, ch=%d",
                         music_info.sample_rates, music_info.bits, music_info.channels);
                i2s_stream_set_clk(i2s_stream_reader, music_info.sample_rates, music_info.bits, music_info.channels);
            }
        }
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == bt_ael_spk) {
            if (msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info;
                audio_element_getinfo(bt_ael_spk, &music_info);

                ESP_LOGI(TAG, "Set clk for i2s_spk, sample_rates=%d, bits=%d, ch=%d",
                         music_info.sample_rates, music_info.bits, music_info.channels);
                i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            }
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_PERIPH && msg.source == EVENT_SOURCE_FROM_BT) {
            auto dat = (bt_event_data_t*)msg.data;
            if (msg.cmd == SPK_ABS_VOL_DATA) {
                i2s_alc_volume_set(i2s_stream_writer, volume_convert_alc(dat->absolute_volume));
            }
            if (msg.cmd == MIC_ABS_VOL_DATA) {
                i2s_alc_volume_set(i2s_stream_reader, volume_convert_alc(dat->absolute_volume));
            }
        }

    }
}

extern "C" int app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

//    hci_dump_init(hci_dump_embedded_stdout_get_instance());

    btstack_stdio_init();
    btstack_init();

    btstack_main();

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_cfg.rb_size = 4 * 1024;
    pipeline_spk = audio_pipeline_init(&pipeline_cfg);
    pipeline_mic = audio_pipeline_init(&pipeline_cfg);

    i2s_stream_cfg_t i2s_spk_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_spk_cfg.i2s_port = I2S_NUM_0;
    i2s_spk_cfg.type = AUDIO_STREAM_WRITER;
    i2s_spk_cfg.use_alc = true;
    i2s_spk_cfg.volume = 64;
    i2s_spk_cfg.i2s_config.mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX);
//    i2s_spk_cfg.i2s_config.sample_rate = 16000;
    i2s_spk_cfg.i2s_config.channel_format = CHANNEL_FMT_SPK;
    i2s_spk_cfg.i2s_config.dma_buf_count = DMA_BUF_COUNT;
    i2s_spk_cfg.i2s_config.dma_buf_len = DMA_BUF_SIZE;
    i2s_spk_cfg.i2s_config.communication_format = I2S_COMM_FORMAT_STAND_MSB;

    i2s_stream_writer = i2s_stream_init(&i2s_spk_cfg);
    i2s_set_pin(I2S_NUM_0, &pin_config_spk);


    i2s_stream_cfg_t i2s_mic_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_mic_cfg.i2s_port = I2S_NUM_1;
    i2s_mic_cfg.type = AUDIO_STREAM_READER;
    i2s_mic_cfg.use_alc = true;
    i2s_mic_cfg.volume = 127;
    i2s_mic_cfg.i2s_config.mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_RX);
    i2s_mic_cfg.i2s_config.sample_rate = 16000;
    i2s_mic_cfg.i2s_config.bits_per_sample = i2s_bits_per_sample_t(16);
    i2s_mic_cfg.i2s_config.channel_format = CHANNEL_FMT_MIC;
    i2s_mic_cfg.i2s_config.dma_buf_count = DMA_BUF_COUNT;
    i2s_mic_cfg.i2s_config.dma_buf_len = DMA_BUF_SIZE;
    i2s_mic_cfg.i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_mic_cfg.i2s_config.use_apll = false;

    i2s_stream_reader = i2s_stream_init(&i2s_mic_cfg);
    i2s_set_pin(I2S_NUM_1, &pin_config_mic);

    audio_pipeline_register(pipeline_spk, bt_ael_spk, "raw_w");
    audio_pipeline_register(pipeline_spk, i2s_stream_writer, "i2s_w");

    audio_pipeline_register(pipeline_mic, i2s_stream_reader, "i2s_r");
    audio_pipeline_register(pipeline_mic, bt_ael_mic, "raw_r");


    const char *link_spk[] = {"raw_w", "i2s_w"};
    audio_pipeline_link(pipeline_spk, &link_spk[0], 2);

    const char *link_mic[] = {"i2s_r", "raw_r"};
    audio_pipeline_link(pipeline_mic, &link_mic[0], 2);

    audio_pipeline_run(pipeline_spk);
    audio_pipeline_run(pipeline_mic);

    thread_t btstack_thread(evt_handler_loop, nullptr);
    btstack_thread.launch();

    btstack_run_loop_execute();
    return 0;
}