//
// Created by ism on 13.01.2024.
//

#ifndef BTSTACK_IMPL_H
#define BTSTACK_IMPL_H

#include "btstack_port_esp32.h"
#include "btstack_run_loop.h"
#include "btstack_stdio_esp32.h"
#include "hci_dump.h"
#include "hci_dump_embedded_stdout.h"

#include "btstack.h"

class bluetooth_controller_t {
public:
    // TODO: connect a2dp.cpp file data callbacks to raw_stream (write - a2dp / write, read - hfp
    // TODO: connect avrcp commands callbacks to event_iface
    // TODO: set event_iface_listener for main_thread messages, including start/stop cmds


    void start() {
        // TODO: create thread
        // Enter run loop (forever)
        btstack_run_loop_execute();
    }

    void stop() {}

    audio_element_handle_t get_sink_audio_element() {}

    audio_element_handle_t get_source_audio_element() {}


private:

    void bt_sink_cb() {}

    void bt_source_cb() {}
};


#endif //BTSTACK_IMPL_H
