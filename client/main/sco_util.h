#ifndef SCO_UTIL_H
#define SCO_UTIL_H

#include "hci.h"

namespace sco_util {
    void init();

    void set_codec(uint8_t codec);

    void send(hci_con_handle_t con_handle);

    void receive(uint8_t *packet, uint16_t size);

    void close();
}

#endif //SCO_UTIL_H
