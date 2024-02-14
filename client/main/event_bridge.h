#ifndef EVENT_BRIDGE_H
#define EVENT_BRIDGE_H

#include <stdint-gcc.h>
#include <esp_event.h>

namespace event_bridge {

    enum cmd_t {
        MIC_ABS_VOL_DATA = 0,
        SPK_ABS_VOL_DATA,
        SVC_START,
        SVC_PAUSE,
        CONNECTION_STATE,
        REQUEST_VOL_DATA
    };

    union data_t {
        struct {
            esp_event_base_t from;
        };
        struct {
            uint8_t absolute_volume; // 0..127
        };
        struct {
            bool connected;
            uint8_t type;
        } conn_state;
        // expand for hfp stuff
    };

    esp_err_t set_listener(esp_event_base_t event_base, esp_event_handler_t event_handler, void* event_handler_arg = nullptr);

    esp_err_t set_listener_specific(esp_event_base_t event_base, int32_t event_id,
                                     esp_event_handler_t event_handler, void* event_handler_arg = nullptr);

    esp_err_t post(esp_event_base_t event_base, int32_t event_id, esp_event_base_t event_from_base,
                   data_t* event_data = nullptr);

}

#endif //EVENT_BRIDGE_H
