/* Play music from Bluetooth device

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <cstddef>
#include <cinttypes>
#include <esp_netif.h>
//#include <protocol_examples_common.h>
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"

#include "esp_log.h"

#include "audio_element.h"
#include "audio_pipeline.h"

#include "common_util.h"
#include "stream_bridge.h"
#include "event_bridge.h"
#include "bt_transport.h"

static const char *TAG = "MAIN";

#define NUM_CHANNELS_SPK 2
#define NUM_CHANNELS_MIC 1

#define USE_FLAC_SPK 0
#define SAMPLE_RATE 44100
//const ip_address_t HOST_ADDR = ip_address_t::from_string("192.168.1.4");
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

//    esp_netif_init();
//    ESP_ERROR_CHECK(esp_event_loop_create_default());
//    ESP_ERROR_CHECK(example_connect());

    stream_bridge::init();

    auto svc_evt = bt_transport::init();

    auto evt_incoming = event_bridge::create_source();
    event_bridge::set_client_listener(svc_evt, evt_incoming);

    event_bridge::send_client_event(svc_evt, event_bridge::SVC_START, nullptr);

    while (true) {
        audio_event_iface_msg_t msg;
        event_bridge::listen(evt_incoming, &msg, true);

        if (msg.source_type == AUDIO_ELEMENT_TYPE_SERVICE) {
            auto dat = reinterpret_cast<event_bridge::data_t *>(msg.data);
            switch (static_cast<event_bridge::cmd_t>(msg.cmd)) {
                case event_bridge::MIC_ABS_VOL_DATA:
                    logi(TAG, "Microphone volume set to %d", dat->absolute_volume);
                    stream_bridge::set_source_volume(dat->absolute_volume);
                    break;
                case event_bridge::SPK_ABS_VOL_DATA:
                    logi(TAG, "Speaker volume set to %d", dat->absolute_volume);
                    stream_bridge::set_sink_volume(dat->absolute_volume);
                    break;
                case event_bridge::REQUEST_VOL_DATA: {
                    auto *data = event_bridge::get_data_container();
                    data->absolute_volume = stream_bridge::get_sink_volume();
                    event_bridge::send_client_event(svc_evt, event_bridge::SPK_ABS_VOL_DATA, data);
                    data = event_bridge::get_data_container();
                    data->absolute_volume = stream_bridge::get_source_volume();
                    event_bridge::send_client_event(svc_evt, event_bridge::MIC_ABS_VOL_DATA, data);
                    break;
                }
                default:
                    break;
            }
        }
    }
    return 0;
}