#ifndef NET_CONTROLLER_H
#define NET_CONTROLLER_H

#include <impl/helpers.h>
#include <cstdint>
#include <cstddef>

#include <sender.h>
#include <receiver.h>

#define DATA_WIDTH 960

#define ACK_TIMEOUT 500

namespace net_controller {

    enum cmd_t {
        CMD_EMPTY = 0,
        CMD_ACK,
        ST_DISCONNECT,
        ST_SPK_ONLY,
        ST_FULL,

        CTL_PLAY_PAUSE,
        CTL_NEXT,
        CTL_PREV
    };

    enum {
        CID_NONE = -1,
        CID_INIT = 0
    };

    union packet_md_t {
        uint16_t data;
        struct {
            uint8_t cmd; // command
            uint8_t cid; // command id
        };
    };

    typedef int (*cmd_cb_t)(cmd_t, void *);

    void init();

    void reset();

    void set_cmd(cmd_t c, bool wait_ack);

    void set_remote_cmd_cb(ctx_func_t<cmd_cb_t> cb);

    void set_remote_ack_cb(ctx_func_t<cmd_cb_t> cb);

    constexpr inline size_t packet_md_size() {
        return sizeof(packet_md_t);
    }

}

#endif //NET_CONTROLLER_H
