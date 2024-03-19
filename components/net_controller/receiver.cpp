#include <receiver.h>
#include <net_controller.h>
#include <net_controller_private.h>

#include <impl/concurrency.h>
#include <impl/log.h>
#include <unistd.h>

namespace receiver {

    static const char *TAG = "RECEIVER";

    static endpoint_t g_endpoint;
    using net_controller::g_socket;

    static ctx_func_t<cb_t> g_cb;
    static mutex_t g_mutex;
    static std::atomic<bool> g_cur_state;
    static thread_t g_thread;

    static semaphore_t g_task_sem;

    static void task_receive(void *ctx);

    void init() {
        memset(&g_endpoint, 0, sizeof g_endpoint);
        g_cb = ctx_func_t<cb_t>();
        mutex_init(&g_mutex);
        g_cur_state = false;
        bin_sem_init(&g_task_sem);

        thread_init(&g_thread, task_receive, "receive_task");
        thread_launch(&g_thread);
    }

    void set_cb(ctx_func_t<cb_t> cb) {
        mutex_lock(&g_mutex);
        g_cb = cb;
        mutex_unlock(&g_mutex);
    }

    void get_endpoint(endpoint_t *enp) {
        mutex_lock(&g_mutex);
        memcpy(enp, &g_endpoint, sizeof(endpoint_t));
        mutex_unlock(&g_mutex);
    }

    void bind(uint16_t port) {
//        close(g_socket);
//        g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
//        if (g_socket == -1) {
//            loge(TAG, "error socket: %d", socket_errno());
//            return;
//        }
        endpoint_t enp;
        endpoint_clear(&enp);
        endpoint_set_port(&enp, port, AF_INET);

        if (bind(g_socket, reinterpret_cast<const sockaddr *>(&enp), sizeof(endpoint_t)) == -1) {
            loge(TAG, "error binding: %d", socket_errno());
            return;
        }
    }

    void start() {
        g_cur_state = true;
        bin_sem_give(&g_task_sem);
    }

    void stop() {
        g_cur_state = false;
        bin_sem_give(&g_task_sem);
    }

    size_t receive(uint8_t *data, size_t bytes) {
        endpoint_t sender_endpoint;
        socklen_t socklen = sizeof(sender_endpoint);
        size_t received = recvfrom(g_socket, reinterpret_cast<char *>(data), bytes, 0,
                                   reinterpret_cast<sockaddr *>(&sender_endpoint), &socklen);
        mutex_lock(&g_mutex);
        g_endpoint = sender_endpoint;
        mutex_unlock(&g_mutex);

        net_controller::remote_set_md(data + received - MD_SIZE);

        return received;
    }

    static void task_receive(void *ctx) {
        logi(TAG, "task_receive is started");
        uint8_t data[PIPE_WIDTH];

        endpoint_t sender_endpoint;
        socklen_t socklen = sizeof(sender_endpoint);
        ssize_t received;
        while (true) {
            if (!g_cur_state) {
                bin_sem_take(&g_task_sem);
                continue;
            }

            received = recvfrom(g_socket, reinterpret_cast<char *>(data), PIPE_WIDTH, 0,
                                reinterpret_cast<sockaddr *>(&sender_endpoint), &socklen);

            if (received == -1) {
                if (errno != EWOULDBLOCK && errno != EAGAIN) loge(TAG, "recvfrom error: %d", errno);
                continue;
            }

            mutex_lock(&g_mutex);

            g_endpoint = sender_endpoint;
            if (received - MD_SIZE > 0) {
                if (g_cb) g_cb(data, received - MD_SIZE);
                else
                    loge(TAG, "no callback specified");
            }

            mutex_unlock(&g_mutex);

            net_controller::remote_set_md(data + received - MD_SIZE);
        }
    }
}