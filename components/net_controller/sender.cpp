#include <sender.h>
#include <net_controller.h>
#include <net_controller_private.h>

#include <impl/concurrency.h>
#include <impl/log.h>
#include <cassert>

namespace sender {

    static const char *TAG = "SENDER";

    using net_controller::g_socket;

    enum {
        FLG_NONE = 0,
        FLG_TASK = (1 << 0),
        FLG_REQ = (1 << 1),
        FLG_REQ_MD = (1 << 2)
    };

    static endpoint_t g_endpoint;

    static uint8_t *g_buf = nullptr;
    static int g_buf_ptr;
    static ctx_func_t<cb_t> g_cb;
    static mutex_t g_mutex;
    static std::atomic<uint8_t> g_cur_flags;
    static thread_t g_thread;

    static semaphore_t g_task_sem, g_req_sem;

    static void send_raw(uint8_t *data, size_t bytes);

    [[noreturn]] static void task_send(void *ctx);

    void init() {
        memset(&g_endpoint, 0, sizeof g_endpoint);
        g_buf = static_cast<uint8_t *>(malloc(PIPE_WIDTH));
        g_buf_ptr = 0;
        g_cb = ctx_func_t<cb_t>();
        mutex_init(&g_mutex);
        g_cur_flags = FLG_NONE;

        bin_sem_init(&g_task_sem);
        bin_sem_init(&g_req_sem);

        thread_init(&g_thread, task_send, "send_task");
        thread_launch(&g_thread);
    }

    void set_endpoint(const endpoint_t *enp) {
        mutex_lock(&g_mutex);
        memcpy(&g_endpoint, enp, sizeof(endpoint_t));
        mutex_unlock(&g_mutex);
    }

    void set_cb(ctx_func_t<cb_t> cb) {
        mutex_lock(&g_mutex);
        g_cb = cb;
        mutex_unlock(&g_mutex);
    }

    void start() {
        g_cur_flags |= FLG_TASK;
        bin_sem_give(&g_task_sem);
    }

    void stop() {
        g_cur_flags &= ~FLG_TASK;
        bin_sem_give(&g_task_sem);
    }

    void send(uint8_t *data, size_t bytes) {
        mutex_lock(&g_mutex);
        while (g_buf_ptr + bytes >= DATA_WIDTH) {
            memcpy(g_buf + g_buf_ptr, data, DATA_WIDTH - g_buf_ptr);
            data += DATA_WIDTH - g_buf_ptr;
            bytes -= DATA_WIDTH - g_buf_ptr;
            g_buf_ptr = DATA_WIDTH;

            g_cur_flags |= FLG_REQ;
            bin_sem_give(&g_task_sem);
            bin_sem_take(&g_req_sem);

            g_buf_ptr = 0;
        }
        memcpy(g_buf + g_buf_ptr, data, bytes);
        g_buf_ptr += bytes;
        mutex_unlock(&g_mutex);
    }

    void send_md() {
        mutex_lock(&g_mutex);
        g_cur_flags |= FLG_REQ_MD;
        bin_sem_give(&g_task_sem);
        bin_sem_take(&g_req_sem);
        mutex_unlock(&g_mutex);
    }

    void send_raw(uint8_t *data, size_t bytes) {
        assert(bytes <= DATA_WIDTH);
        net_controller::remote_get_md(data + bytes);
        sendto(g_socket, reinterpret_cast<char *>(data), bytes + MD_SIZE, 0, reinterpret_cast<sockaddr *>(&g_endpoint),
               sizeof(endpoint_t));
    }

    [[noreturn]] void task_send(void *ctx) {
        logi(TAG, "task_send is started");

        while (true) {
            if (!(g_cur_flags & FLG_TASK)) bin_sem_take(&g_task_sem);

            if (g_cur_flags & FLG_REQ) {
                send_raw(g_buf, g_buf_ptr);

                g_cur_flags &= ~FLG_REQ;
                goto MDSent;
            }

            if (g_cur_flags & FLG_TASK) {
                size_t bytes = 0;

                mutex_lock(&g_mutex);
                if (g_cb) bytes = g_cb(g_buf + g_buf_ptr, DATA_WIDTH - g_buf_ptr);
                else
                    loge(TAG, "no callback specified");

                assert(bytes <= DATA_WIDTH - g_buf_ptr);
                g_buf_ptr += bytes;

                send_raw(g_buf, g_buf_ptr);
                if (g_buf_ptr == DATA_WIDTH) g_buf_ptr = 0;
                mutex_unlock(&g_mutex);

                goto MDSent;
            }

            if (g_cur_flags & FLG_REQ_MD) {
                uint8_t mdbuf[MD_SIZE];
                send_raw(mdbuf, 0);

                MDSent:
                g_cur_flags &= ~FLG_REQ_MD;
                bin_sem_give(&g_req_sem);
            }

        }
    }
}