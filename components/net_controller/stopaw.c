#include <net_controller.h>
#include <netctl_defs.h>

#include <impl/concurrency.h>
#include <impl/queue.h>
#include <impl/log.h>
#include <impl/time.h>

#include <string.h>
#include <unistd.h>
#include <stdlib.h>

enum netctl_stopaw_data_type_t {
    STOPAW_CONN_T = 0xAA,
    STOPAW_DISCONN_T = 0x66,
    STOPAW_PING_T = 0xB1,
    STOPAW_STATUS_T = 0xE3
};

// TODO: compare stopaw footprint with tcp socket footprint
// can simplify code using another socket just for ping/connection
enum netctl_stopaw_status_t {
    STATUS_OK = 0,
    STATUS_FAIL
};

enum netctl_stopaw_ret_t {
    OK,
    ERR_TIMEOUT,
    ERR_RESPONSE,
    ERR_FAILED
};

typedef struct {
    union {
        struct {
            uint64_t rtt;
        } conn;

        struct {
            uint8_t val;
        } status;
    };

    uint8_t type;
} netctl_stopaw_data_t;

typedef struct {
    netctl_stopaw_data_t data;

    uint8_t ack;

    // this is common for stream and stopaw
    uint8_t md_type;
} netctl_stopaw_md_t __attribute__((packed));

extern socket_t netctl_sock;

// either 0 or 1
static uint8_t g_stopaw_ack = 0;


static int stopaw_send(endpoint_t *enp, netctl_stopaw_data_t *data, time_t timeout) {
    netctl_stopaw_md_t send_md;
    netctl_stopaw_md_t recv_md;

    socklen_t socklen = sizeof(endpoint_t);
    endpoint_t recv_enp;

    int ret;
    time_t cur_tm;

    memcpy(&send_md.data, data, sizeof(netctl_stopaw_data_t));
    send_md.ack = g_stopaw_ack;
    send_md.md_type = MD_STOPAW_T;

    cur_tm = time_micros();
    while (true) {
        if (time_micros() - cur_tm >= timeout) {
            ret = ERR_TIMEOUT;
            break;
        }
        sendto(netctl_sock,
               &send_md,
               sizeof(send_md),
               0,
               enp,
               sizeof(endpoint_t));

        ret = recvfrom(netctl_sock,
                       &recv_md,
                       sizeof(recv_md),
                       0,
                       &recv_enp,
                       &socklen);

        if (ret < 0 // timeout
            || memcmp(enp, &recv_enp, socklen) != 0 // wrong endpoint
            || ret != sizeof(recv_md)) { // incorrect packet size
            continue;
        }
        if (recv_md.md_type != MD_STOPAW_T
            || recv_md.ack != send_md.ack) {
            continue;
        }
        g_stopaw_ack = (g_stopaw_ack + 1) % 2;

        if (recv_md.data.type != STOPAW_STATUS_T) {
            continue;
        }

        if (recv_md.data.status.val != STATUS_OK) {
            loge("Endpoint replied error");
            ret = ERR_RESPONSE;
            break;
        }

        ret = OK;
        break;
    }
    return ret;
}

static time_t ping(endpoint_t *enp, time_t packet_timeout, int n) {
    if (netctl_sock < 0) {
        return 0;
    }

    netctl_stopaw_data_t ping_data;
    ping_data.type = STOPAW_PING_T;

    int ret;
    time_t cur_tm, tm = 0;

    for (int i = 0; i < n; ++i) {
        cur_tm = time_micros();
        ret = stopaw_send(enp, &ping_data, packet_timeout);
        if (ret == ERR_TIMEOUT) {
            return 0;
        }
        tm += time_micros() - cur_tm;
    }
    tm /= n;
    return tm;
}


int netctl_connect(const endpoint_t *enp, time_t timeout) {
    if (netctl_sock < 0) {
        return 0;
    }

    netctl_stopaw_data_t conn_data;
    int ret;
    conn_data.conn.rtt = ping(enp, timeout, 4);
    if (conn_data.conn.rtt == 0) {
        loge("Couldn't ping endpoint");
        return -1;
    }

    ret = stopaw_send(enp, &conn_data, timeout);
    if (ret == OK) {
        return 0;
    }
    if (ret == ERR_RESPONSE) {
        loge("Endpoint refused to connect");
    }
    return -1;
}

int netctl_disconnect(time_t timeout) {

}
