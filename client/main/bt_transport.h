#ifndef BT_TRANSPORT_H
#define BT_TRANSPORT_H

#include "event_bridge.h"

namespace bt_transport {

    event_bridge::source_t init();

    enum connection_type_t {
        CONN_TYPE_AVRCP,
        CONN_TYPE_A2DP,
        CONN_TYPE_HFP,
        CONN_TYPE_HCI
    };

}

#endif //BT_TRANSPORT_H
