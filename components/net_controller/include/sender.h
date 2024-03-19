#ifndef NET_CONTROLLER_SENDER_H
#define NET_CONTROLLER_SENDER_H

#include <impl/helpers.h>
#include <impl/socket.h>
#include <cstdint>

namespace sender {

    typedef size_t (*cb_t)(uint8_t *, size_t, void *);

    void init();

    void set_cb(ctx_func_t<cb_t> cb);

    void set_endpoint(const endpoint_t *enp);

    void start();

    void stop();

    void send(uint8_t *data, size_t bytes);

    void send_md();

}


#endif //NET_CONTROLLER_SENDER_H
