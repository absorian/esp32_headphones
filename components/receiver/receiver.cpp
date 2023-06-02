//
// Created by ism on 01.06.2023.
//

#include "receiver.h"

receiver_t::receiver_t(controller_t *controller, size_t pipe_width) : control(controller),
                                                                      recv_thread(task_receive, this),
                                                                      width(pipe_width) {}

receiver_t::receiver_t(size_t pipe_width) : receiver_t(nullptr, pipe_width) {}

void receiver_t::set_receive_callback(receive_callback_t cb, void *client_data_) {
    mutex.lock();
    client_data = client_data_;
    recv_cb = cb;
    mutex.unlock();
}

udp_endpoint_t receiver_t::get_endpoint() {
    mutex.lock();
    udp_endpoint_t enp = endpoint;
    mutex.unlock();
    return enp;
}

void receiver_t::start(uint16_t port) {
    socket.unbind();
    socket.bind(port);
    recv_thread.launch();
}

void receiver_t::stop() {
    mutex.lock();
    recv_thread.terminate();
    mutex.unlock();
}

void receiver_t::task_receive(void *param) {
    auto body = (receiver_t *) param;

    logi(body->TAG, "task_receive is started");
    uint8_t data[body->width];

    udp_endpoint_t sender_endpoint;
    size_t received;
    while (true) {
        received = body->socket.receive(data, body->width, sender_endpoint);

        body->mutex.lock();

        body->endpoint = sender_endpoint;
        if (received - controller_t::md_size() > 0)
            body->recv_cb(data, received - controller_t::md_size(), body->client_data);

        body->mutex.unlock();

        if (body->control != nullptr)
            body->control->remote_set_md(data + received - controller_t::md_size());
    }
}
