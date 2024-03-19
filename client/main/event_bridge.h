#ifndef EVENT_BRIDGE_H
#define EVENT_BRIDGE_H

#include <cstdint>
#include <esp_event.h>

namespace event_bridge {

    enum cmd_t {
        VOL_DATA_MIC = 0,
        VOL_DATA_SPK,
        VOL_DATA_RQ,

        SVC_START, // TRT_START
        SVC_PAUSE, // TRT_STOP .. TRT_DISCONNECT, TRT_CONNECT {creds}

        CTL_SWITCH_SVC,
        CTL_GO_SLEEP
    };

    union data_t {
        struct {
            esp_event_base_t from;
        };
        struct {
            uint8_t absolute_volume; // 0..127
        };
    };

    esp_err_t init();

    esp_err_t set_listener(esp_event_base_t event_base, esp_event_handler_t event_handler, void* event_handler_arg = nullptr);

    esp_err_t set_listener_specific(esp_event_base_t event_base, int32_t event_id,
                                     esp_event_handler_t event_handler, void* event_handler_arg = nullptr);

    esp_err_t post(esp_event_base_t event_base, int32_t event_id, esp_event_base_t event_from_base,
                   data_t* event_data = nullptr);

    esp_err_t post_isr(esp_event_base_t event_base, int32_t event_id, esp_event_base_t event_from_base,
                   data_t* event_data = nullptr);

}

#endif //EVENT_BRIDGE_H
