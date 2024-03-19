#include <net_controller.h>
#include <net_controller_private.h>

#include <impl/concurrency.h>
#include <impl/socket.h>
#include <impl/helpers.h>
#include <impl/log.h>

namespace net_controller {

    static const char *TAG = "NET_CONTROLLER";

    socket_t g_socket;

    static ctx_func_t<cmd_cb_t> remote_cmd_cb;
    static ctx_func_t<cmd_cb_t> remote_ack_cb;

    static mutex_t mutex;
    static semaphore_t ack_sem;
    static bool ack_dowait;

    static cmd_t cmd;
    static int cid;

    static int need_ack_cid;
    static int last_ack_cid;


    void init() {
        socket_init();
        g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (g_socket == -1) {
            loge(TAG, "Socket init error: %d", socket_errno());
            return;
        }
//        int opt = 1;
//        if (setsockopt(g_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&opt), sizeof(int)) == -1) {
//            loge(TAG, "error setting SO_REUSEADDR: %d", socket_errno());
//            return;
//        }

        mutex_init(&mutex);
        bin_sem_init(&ack_sem);
        ack_dowait = false;

        cmd = CMD_EMPTY;
        cid = CID_INIT;
        need_ack_cid = CID_NONE;
        last_ack_cid = CID_NONE;

        sender::init();
        receiver::init();
    }

    void reset() {
        sender::stop();
        receiver::stop();

        mutex_lock(&mutex);
        cmd = CMD_EMPTY;
        cid = CID_INIT;
        need_ack_cid = CID_NONE;
        last_ack_cid = CID_NONE;
        if (ack_dowait) {
            bin_sem_give(&ack_sem);
            ack_dowait = false;
        }
        mutex_unlock(&mutex);
    }

    void set_cmd(cmd_t c, bool wait_ack) {
        if (c == CMD_ACK || c == CMD_EMPTY) return;

        mutex_lock(&mutex);
        if (cmd != CMD_EMPTY) {
            loge(TAG, "The previous command was not obtained: %d", cmd);
            mutex_unlock(&mutex);
            return;
        }
        cmd = c;
        if (++cid > 255) cid = 0;
        ack_dowait = wait_ack;
        mutex_unlock(&mutex);

        sender::send_md();
        if (!wait_ack) return;

        if (bin_sem_take(&ack_sem, ACK_TIMEOUT) == -1) {
            loge(TAG, "Command (%d) acknowledgement timed out", cmd);
            mutex_lock(&mutex);
            ack_dowait = false;
            cmd = CMD_EMPTY;
            mutex_unlock(&mutex);
        }
    }

    void remote_set_md(const uint8_t *d) {
        auto *data = (packet_md_t *) d;
        if (data->cmd == CMD_EMPTY) return;

        cmd_t cb_cmd = CMD_EMPTY, cb_ack_cmd = CMD_EMPTY;

        mutex_lock(&mutex);
        if (data->cmd == CMD_ACK) {
            if (data->cid == cid) {
                cb_ack_cmd = cmd;
                cmd = CMD_EMPTY;
                if (ack_dowait) {
                    bin_sem_give(&ack_sem);
                    ack_dowait = false;
                }
            }
        } else {
            if (last_ack_cid != data->cid) {
                cb_cmd = static_cast<cmd_t>(data->cmd);
                last_ack_cid = data->cid;
            }
        }
        mutex_unlock(&mutex);

        if (cb_cmd != CMD_EMPTY) {
            int res = remote_cmd_cb(cb_cmd);
            if (res == 0) {
                mutex_lock(&mutex);
                need_ack_cid = data->cid;
                mutex_unlock(&mutex);
                sender::send_md();
            }
        }
        if (cb_ack_cmd != CMD_EMPTY) {
            int res = remote_ack_cb(cb_ack_cmd);
        }
    }

    void remote_get_md(uint8_t *d) {
        auto *data = (packet_md_t *) d;
        mutex_lock(&mutex);
        if (need_ack_cid != -1) {
            data->cmd = CMD_ACK;
            data->cid = need_ack_cid;
            need_ack_cid = -1;
        } else {
            data->cmd = cmd;
            data->cid = cid;
        }
        mutex_unlock(&mutex);
    }

    void set_remote_cmd_cb(ctx_func_t<cmd_cb_t> cb) {
        mutex_lock(&mutex);
        remote_cmd_cb = cb;
        mutex_unlock(&mutex);
    }

    void set_remote_ack_cb(ctx_func_t<cmd_cb_t> cb) {
        mutex_lock(&mutex);
        remote_ack_cb = cb;
        mutex_unlock(&mutex);
    }

}