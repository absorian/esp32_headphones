#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <impl/socket.h>

// TODO: move to menuconfig
#define NETCTL_BATCH_SZ 10 // uint8
#define NETCTL_ACK_BUF_SZ 3
#define NETCTL_REPEAT_SEND_INTERVAL 10000
#define NETCTL_PORT 48080

#define _NETCTL_PAYLOAD_MAX_SZ (1024 - sizeof(netctl_md_t))
#define NETCTL_PAYLOAD_MAX_SZ ((_NETCTL_PAYLOAD_MAX_SZ / 4) * 4)

// NOTE: time_t represents us (microseconds)
#define NETCTL_TIME_BLOCK (~(time_t)0)
#define NETCTL_TIME_ASYNC 0

enum netctl_ctl_cmd_t {
    NETCTL_EMPTY = 0,

    NETCTL_CONNECT,
    NETCTL_DISCONNECT,

    NETCTL_PLAYPAUSE,
    NETCTL_NEXT,
    NETCTL_PREV,

    NETCTL_VOLUP,
    NETCTL_VOLDOWN
};

typedef struct {
    // although maximum for user to put here is NETCTL_PAYLOAD_MAX_SZ
    // 1024 just for internal needs
    uint8_t payload[1024];
    int payload_sz;
} netctl_payload_t __attribute__((packed));

typedef struct {
    uint8_t data;
    uint8_t cmd;
} netctl_ctl_t __attribute__((packed));

int netctl_init();

int netctl_deinit();

int netctl_connect(const endpoint_t *enp, time_t timeout);

int netctl_disconnect(time_t timeout);

// check whether connected
bool netctl_status();

// repeated call will discard the previous
int netctl_send_next_batch(netctl_payload_t data[], time_t timeout);

// discard sending process of the last provided data
// if not already sent
void netctl_send_stop();

int netctl_recv_next_batch(time_t timeout);

// tell netctl to stop receiving and finalize the data in provided container
netctl_payload_t *netctl_recv_stop();


bool netctl_send_complete();

// not used for now
bool netctl_recv_complete();


int netctl_enqueue_ctl(netctl_ctl_t *ctl);

int netctl_recv_ctl(netctl_ctl_t *ctl, time_t timeout);
