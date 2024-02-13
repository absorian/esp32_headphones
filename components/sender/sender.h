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
    sender_t(controller_t *controller, size_t pipe_width);

    void set_endpoint(const udp_endpoint_t &enp);

    void send(uint8_t *data, size_t bytes);

    void send_immediate(uint8_t *data, size_t bytes);

private:
    void send_raw(uint8_t *data, size_t bytes);

    controller_t *control;

    std::vector<uint8_t> buf;
    int buf_ptr = 0;

    udp_socket_t socket;
    udp_endpoint_t endpoint;
public:
    const size_t width;
};

#endif //SENDER_H
