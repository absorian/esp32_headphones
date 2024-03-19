#ifndef NET_CONTROLLER_RECEIVER_H
#define NET_CONTROLLER_RECEIVER_H

#include <impl/helpers.h>
#include <impl/socket.h>

#include <cstdint>

namespace receiver {

    typedef void (*cb_t)(const uint8_t *, size_t, void *);

    void init();

    void set_cb(ctx_func_t<cb_t> cb);

    void get_endpoint(endpoint_t *enp);

    void bind(uint16_t port);

    void start();

    void stop();

    size_t receive(uint8_t *data, size_t bytes);

}


#endif //NET_CONTROLLER_RECEIVER_H
