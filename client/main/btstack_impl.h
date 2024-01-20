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

#include "audio_element.h"
#include "raw_stream.h"

extern audio_element_handle_t bt_ael_mic;
extern audio_element_handle_t bt_ael_spk;

extern uint8_t sdp_avdtp_sink_service_buffer[150]; // TODO: needs to be reworked
typedef void* event_bt_source_t;
#define EVENT_SOURCE_FROM_BT event_bt_source_t(sdp_avdtp_sink_service_buffer)

long map(long x, long in_min, long in_max, long out_min, long out_max);

extern audio_event_iface_handle_t bt_evt_iface;

enum bt_event_cmd_t {
    MIC_ABS_VOL_DATA = 0,
    SPK_ABS_VOL_DATA,

};

union bt_event_data_t {
    struct {
        uint8_t absolute_volume; // 0..127
    };
    // expand for hfp stuff
};

int btstack_main();

class bluetooth_controller_t {
public:
    // TODO: connect a2dp.cpp file data callbacks to raw_stream (write - a2dp / write, read - hfp
    // TODO: connect avrcp commands callbacks to event_iface
    // TODO: set event_iface_listener for main_thread messages, including start/stop cmds
};


#endif //BTSTACK_IMPL_H
