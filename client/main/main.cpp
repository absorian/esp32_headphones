/* Play music from Bluetooth device

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <cstddef>
#include <cinttypes>
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

#include "common_util.h"
#include "stream_bridge.h"
#include "event_bridge.h"
#include "bt_transport.h"
#include "net_transport.h"
#include "wifi_util.h"
#include "ctl_periph.h"

#include <impl.h>

static const char *TAG = "MAIN";

static const esp_event_base_t trts[] = {
        NET_TRANSPORT,
        BT_TRANSPORT
};
static const int trt_count = sizeof(trts) / sizeof(trts[0]);
static int cur_trt = 0;

static void main_event_cb(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    auto dat = reinterpret_cast<event_bridge::data_t *>(event_data);
    switch (static_cast<event_bridge::cmd_t>(event_id)) {
        case event_bridge::VOL_DATA_MIC:
            logi(TAG, "Microphone volume set to %d", dat->absolute_volume);
            stream_bridge::set_source_volume(dat->absolute_volume);
            break;
        case event_bridge::VOL_DATA_SPK:
            logi(TAG, "Speaker volume set to %d", dat->absolute_volume);
            stream_bridge::set_sink_volume(dat->absolute_volume);
            break;
        case event_bridge::VOL_DATA_RQ: {
            logi(TAG, "Volume data request from %s", dat->from);
            event_bridge::data_t evt_data{};

            evt_data.absolute_volume = stream_bridge::get_sink_volume();
            event_bridge::post(dat->from, event_bridge::VOL_DATA_SPK, APPLICATION, &evt_data);

            evt_data.absolute_volume = stream_bridge::get_source_volume();
            event_bridge::post(dat->from, event_bridge::VOL_DATA_MIC, APPLICATION, &evt_data);
            break;
        }
        case event_bridge::CTL_SWITCH_SVC: {
            logi(TAG, "Service switch request from %s", dat->from);
            event_bridge::post(trts[cur_trt], event_bridge::SVC_PAUSE, APPLICATION);

            cur_trt++;
            if (cur_trt >= trt_count) cur_trt = 0;
            logi(TAG, "New transport: %s (%d)", trts[cur_trt], cur_trt);
            event_bridge::post(trts[cur_trt], event_bridge::SVC_START, APPLICATION);
        }
        default:
            break;
    }
}


extern "C" [[noreturn]] int app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    esp_log_level_set("*", ESP_LOG_INFO);

    event_bridge::init();
    stream_bridge::init();
    ctl_periph::init();

    net_transport::init();
    bt_transport::init();

    event_bridge::set_listener(APPLICATION, main_event_cb);
    event_bridge::post(trts[cur_trt], event_bridge::SVC_START, APPLICATION);

    while (true) {
        thread_t::sleep(500);
    }
}