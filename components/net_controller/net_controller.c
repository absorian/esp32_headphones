#include <net_controller.h>
#include <netctl_defs.h>

#include <impl/concurrency.h>
#include <impl/queue.h>
#include <impl/log.h>
#include <impl/time.h>

#include <string.h>
#include <unistd.h>
#include <stdlib.h>

typedef struct {
    uint8_t packet_num;
    uint8_t batch_num;
} netctl_packet_id_t __attribute__((packed));

typedef struct {
    netctl_packet_id_t id;
    netctl_packet_id_t acks[NETCTL_ACK_BUF_SZ];
    netctl_ctl_t ctl;

    // this is common for stream and stopaw
    uint8_t md_type;
} netctl_md_t __attribute__((packed));


socket_t netctl_sock = -1;

static endpoint_t g_enp;

static bool g_send_enable;
static semaphore_t g_send_trigger_sem;
static mutex_t g_send_mutex;

static bool g_recv_enable;
static semaphore_t g_recv_trigger_sem;
static mutex_t g_recv_mutex;

static queue_t g_send_ctl_queue;
static queue_t g_recv_ctl_queue;

// either 0 or 1
static uint8_t g_send_batch_num = 0;
static uint8_t g_recv_batch_num = 0;

// Protecting access to single values alone is not necessary
// https://esp32.com/viewtopic.php?t=18771
// TODO: make enum for ack states
// 0 - sent, 2 - acknowledged
static uint8_t g_send_acks[NETCTL_BATCH_SZ];
// the state 1 is to not to receive duplicate packets
// 0 - not received, 1 - received, 2 - acknowledged
static uint8_t g_recv_acks[NETCTL_BATCH_SZ];

// TODO: check if the image size is affected with static storage
static netctl_payload_t g_send_payload[NETCTL_BATCH_SZ];
static netctl_payload_t g_recv_payload[NETCTL_BATCH_SZ];

static thread_t g_send_thread;
static thread_t g_recv_thread;


static void send_payload(netctl_payload_t *payload) {
    sendto(netctl_sock,
           payload->payload,
           payload->payload_sz,
           0,
           &g_enp,
           sizeof(endpoint_t));
}

static int recv_payload(netctl_payload_t *payload) {
    socklen_t socklen = sizeof(g_enp);
    endpoint_t recv_enp;

    int ret = recvfrom(netctl_sock,
                       payload->payload,
                       payload->payload_sz,
                       0,
                       &recv_enp,
                       &socklen);
    if (ret < 0) return ret;
    if (memcmp(&g_enp, &recv_enp, socklen) != 0)
        return -1;

    return 0;
}

static void task_send(thread_t *handle, void *ctx) {
    while (!thread_exit_requested(handle)) {
        mutex_lock(&g_send_mutex);
        if (!g_send_enable) {
            mutex_unlock(&g_send_mutex);
            bin_sem_wait(&g_send_trigger_sem);
            continue;
        }
        for (int i = 0; i < NETCTL_BATCH_SZ; ++i) {
            if (g_send_acks[i] == 0)
                send_payload(&g_send_payload[i]);
        }
        mutex_unlock(&g_send_mutex);
        time_sleep(NETCTL_REPEAT_SEND_INTERVAL);
    }
}

static void task_recv(thread_t *handle, void *ctx) {
    netctl_payload_t data;
    while (!thread_exit_requested(handle)) {
        mutex_lock(&g_recv_mutex);
        if (!g_recv_enable) {
            mutex_unlock(&g_recv_mutex);
            bin_sem_wait(&g_recv_trigger_sem);
            continue;
        }
        if (recv_payload(&data) < 0) goto cont;

        uint8_t last_byte = *(data.payload + data.payload_sz - 1);

        // discard if magic md type doesn't match
        if (last_byte != MD_STREAM_T) goto cont;
        netctl_md_t *md = (netctl_md_t *) (data.payload + data.payload_sz - sizeof(netctl_md_t));

        // discard if the batch num doesn't match
        if (md->id.batch_num != g_recv_batch_num) goto cont;

        // discard if the packet was already received
        if (g_recv_acks[md->id.packet_num] != 0) goto cont;

        // tell that packet is received
        g_recv_acks[md->id.packet_num] = 1;

        // enqueue ctl if not empty
        if (md->ctl.cmd != NETCTL_EMPTY) {
            queue_enqueue(&g_recv_ctl_queue, &md->ctl);
        }

        for (int i = 0; i < NETCTL_ACK_BUF_SZ; ++i) {
            netctl_packet_id_t *id = &md->acks[i];
            if (id->batch_num != g_send_batch_num || id->packet_num >= NETCTL_BATCH_SZ)
                continue;

            g_send_acks[id->packet_num] = 2;
        }

        memcpy(&g_recv_payload[md->id.packet_num], &data, sizeof(netctl_payload_t));
    cont:
        mutex_unlock(&g_recv_mutex);
    }
}

int netctl_init() {
    if (netctl_sock >= 0) {
        return 0;
    }

    // require the user to call it
    // socket_init();

    netctl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (netctl_sock == -1) {
        loge("Socket failed to init: %d", socket_errno());
        return -1;
    }

    struct timeval timeo = {
        .tv_sec = 0,
        .tv_usec = 1000 // 1ms
    };
    if (setsockopt(netctl_sock, SOL_SOCKET, SO_RCVTIMEO, &timeo, sizeof(timeo)) < 0) {
        loge("Failed to set SO_RCVTIMEO: %d", socket_errno());
        return -1;
    }

    endpoint_clear(&g_enp);
    endpoint_set_port(&g_enp, NETCTL_PORT);
    if (bind(netctl_sock, &g_enp, sizeof(endpoint_t)) == -1) {
        loge("Failed to bind: %d", socket_errno());
        return -1;
    }

    thread_func_t func;
    func.call = task_send;
    thread_init(&g_send_thread, &func);
    func.call = task_recv;
    thread_init(&g_recv_thread, &func);

    bin_sem_init(&g_send_trigger_sem);
    bin_sem_init(&g_recv_trigger_sem);

    mutex_init(&g_send_mutex);
    mutex_init(&g_recv_mutex);

    queue_init(&g_send_ctl_queue, 5, sizeof(netctl_ctl_t));
    queue_init(&g_recv_ctl_queue, 5, sizeof(netctl_ctl_t));

    return 0;
}

int netctl_deinit() {
    if (netctl_sock < 0) {
        return 0;
    }

    close(netctl_sock);
    netctl_sock = -1;

    thread_exit(&g_send_thread);
    if (!g_send_enable) {
        bin_sem_give(&g_send_trigger_sem);
    }

    thread_exit(&g_recv_thread);
    if (!g_recv_enable) {
        bin_sem_give(&g_recv_trigger_sem);
    }

    bin_sem_deinit(&g_send_trigger_sem);
    bin_sem_deinit(&g_recv_trigger_sem);

    mutex_deinit(&g_send_mutex);
    mutex_deinit(&g_recv_mutex);

    queue_deinit(&g_send_ctl_queue);
    queue_deinit(&g_recv_ctl_queue);

    return 0;
}

bool netctl_status() {
}

int netctl_send_next_batch(netctl_payload_t data[], time_t timeout) {
    mutex_lock(&g_send_mutex);

    // switch batch num
    g_send_batch_num = (g_send_batch_num + 1) % 2;
    // form the batch
    for (int i = 0; i < NETCTL_BATCH_SZ; ++i) {
        // reset ack
        g_send_acks[i] = 0;

        // fill the packet
        netctl_md_t md;

        md.md_type = MD_STREAM_T;

        if (queue_dequeue(&g_send_ctl_queue, &md.ctl, 0) < 0) {
            md.ctl.cmd = NETCTL_EMPTY;
        }

        md.id = netctl_packet_id_t{
            .batch_num = g_send_batch_num,
            .packet_num = i
        };

        int ack_i = 0;
        for (int j = 0; j < NETCTL_BATCH_SZ; ++j) {
            if (g_recv_acks[j] != 1) continue;

            md.acks[ack_i].batch_num = g_recv_batch_num;
            md.acks[ack_i].packet_num = j;
            g_recv_acks[j] = 2;
            if (++ack_i == NETCTL_ACK_BUF_SZ) break;
        }

        // pack the data
        netctl_payload_t *payload = &g_send_payload[i];
        memcpy(payload, &data[i], sizeof(netctl_payload_t));

        memcpy(payload->payload + payload->payload_sz, &md, sizeof(netctl_md_t));
        payload->payload_sz += sizeof(netctl_md_t);
    }
    if (!g_send_enable) {
        g_send_enable = true;
        bin_sem_give(&g_send_trigger_sem);
    }

    mutex_unlock(&g_send_mutex);

    if (timeout != NETCTL_TIME_ASYNC) {
        time_sleep(timeout);
        netctl_send_stop();
    }
    return 0;
}

void netctl_send_stop() {
    mutex_lock(&g_send_mutex);
    g_send_enable = false;
    mutex_unlock(&g_send_mutex);
}

int netctl_recv_next_batch(time_t timeout) {
    mutex_lock(&g_recv_mutex);

    memset(g_recv_acks, 0, sizeof(g_recv_acks));
    memset(g_recv_payload, 0, sizeof(g_recv_payload));
    g_recv_batch_num = (g_recv_batch_num + 1) % 2;

    if (!g_recv_enable) {
        g_recv_enable = true;
        bin_sem_give(&g_recv_trigger_sem);
    }
    mutex_unlock(&g_recv_mutex);

    if (timeout != NETCTL_TIME_ASYNC) {
        time_sleep(timeout);
        netctl_recv_stop();
    }
    return 0;
}

netctl_payload_t *netctl_recv_stop() {
    mutex_lock(&g_recv_mutex);
    g_recv_enable = false;
    mutex_unlock(&g_recv_mutex);

    return g_recv_payload;
}

bool netctl_send_complete() {
}

bool netctl_recv_complete() {
}

int netctl_enqueue_ctl(netctl_ctl_t *ctl) {
    return queue_enqueue(&g_send_ctl_queue, ctl);
}

int netctl_recv_ctl(netctl_ctl_t *ctl, time_t timeout) {
    return queue_dequeue(&g_recv_ctl_queue, ctl, timeout);
}
