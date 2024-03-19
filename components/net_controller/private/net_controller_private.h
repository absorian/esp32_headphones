#ifndef NET_CONTROLLER_PRIVATE_H
#define NET_CONTROLLER_PRIVATE_H

#include <net_controller.h>

#include <impl/socket.h>
#include <impl/concurrency.h>

#include <cstdint>
#include <atomic>

#define MD_SIZE net_controller::packet_md_size()
#define PIPE_WIDTH DATA_WIDTH + MD_SIZE

namespace net_controller {

    void remote_set_md(const uint8_t *d);

    void remote_get_md(uint8_t *d);

    extern socket_t g_socket;

}

#endif //NET_CONTROLLER_PRIVATE_H
