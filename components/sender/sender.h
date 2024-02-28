//
// Created by ism on 01.06.2023.
//

#ifndef SENDER_H
#define SENDER_H

#include "controller.h"
#include "impl.h"

class sender_t {
    static constexpr char TAG[] = "Sender";
public:
    typedef size_t (*send_callback_t)(uint8_t *, size_t, void *);

    sender_t(controller_t *controller, size_t pipe_width);

    void start();

    void stop();

    void set_endpoint(const udp_endpoint_t &enp);

    void send(uint8_t *data, size_t bytes);

    void send_immediate(uint8_t *data, size_t bytes);

private:
    [[noreturn]] static void task_send(void* ctx);

    static size_t send_callback_dummy(uint8_t *, size_t, void *) {
        return 0;
    }

    void send_buffered(uint8_t *data, size_t bytes);

    void send_raw(uint8_t *data, size_t bytes);

    controller_t *control;

    uint8_t *buf;
    int buf_ptr = 0;

    udp_socket_t socket;
    udp_endpoint_t endpoint;

    send_callback_t send_cb = send_callback_dummy;
    void *client_data = nullptr;
    thread_t send_thread;
    mutex_t mutex;

public:
    const size_t width;
};

#endif //SENDER_H
