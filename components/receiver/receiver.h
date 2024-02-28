//
// Created by ism on 01.06.2023.
//

#ifndef RECEIVER_H
#define RECEIVER_H

#include <cstdint>
#include "controller.h"
#include "impl.h"

class receiver_t {
    static constexpr char TAG[] = "Receiver";
public:
    typedef void (*receive_callback_t)(const uint8_t *, size_t, void *);

    receiver_t(controller_t *controller, size_t pipe_width);

    explicit receiver_t(size_t pipe_width);

    void set_receive_callback(receive_callback_t cb, void *client_data_ = nullptr);

    udp_endpoint_t get_endpoint();

    void start(uint16_t port);

    void start();

    void stop();

    size_t receive(uint8_t *data, size_t bytes);

private:
    [[noreturn]] static void task_receive(void *param);

    static void receive_callback_dummy(const uint8_t *, size_t, void *) {}

    controller_t *control;

    receive_callback_t recv_cb = receive_callback_dummy;
    void *client_data = nullptr;
    thread_t recv_thread;
    mutex_t mutex;

    udp_socket_t socket;
    udp_endpoint_t endpoint;
public:
    const size_t width;
};

#endif //RECEIVER_H
