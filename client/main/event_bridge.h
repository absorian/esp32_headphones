#ifndef EVENT_BRIDGE_H
#define EVENT_BRIDGE_H

#include <stdint-gcc.h>
#include <audio_event_iface.h>

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
            uint8_t absolute_volume; // 0..127
        };
        struct {
            bool connected;
            uint8_t type;
        } conn_state;
        // expand for hfp stuff
    };

    typedef audio_event_iface_handle_t source_t;
    typedef audio_event_iface_msg_t message_t;

    source_t create_source();

    data_t *get_data_container();

    void send_service_event(source_t source, cmd_t cmd, data_t *data); // from service to client

    void send_client_event(source_t source, cmd_t cmd, data_t *data); // from client to service

    void set_client_listener(source_t source, source_t listener);

    void set_service_listener(source_t source, source_t listener);

    int listen(source_t source, message_t *msg, bool block);
}

#endif //EVENT_BRIDGE_H
