//
// Created by ism on 01.06.2023.
//

#include "sender.h"

sender_t::sender_t(controller_t *controller, size_t pipe_width) : control(controller),
                                                                  buf(static_cast<uint8_t *>(malloc(
                                                                          pipe_width + controller_t::md_size()))),
                                                                  send_thread(sender_t::task_send, this),
                                                                  width(pipe_width) {}

void sender_t::start() {
    mutex.lock();
    send_thread.launch();
    mutex.unlock();
}

void sender_t::stop() {
    mutex.lock();
    send_thread.terminate();
    mutex.unlock();
}

void sender_t::set_endpoint(const udp_endpoint_t &enp) {
    mutex.lock();
    endpoint = enp;
    mutex.unlock();
}

void sender_t::send_buffered(uint8_t *data, size_t bytes) {
    while (buf_ptr + bytes >= width) {
        memcpy(buf + buf_ptr, data, width - buf_ptr);
        send_raw(buf, width);
        data += width - buf_ptr;
        bytes -= width - buf_ptr;
        buf_ptr = 0;
    }
    if (!bytes) {
        mutex.unlock();
        return;
    }
    memcpy(buf + buf_ptr, data, bytes);
    buf_ptr += bytes;
}

void sender_t::send(uint8_t *data, size_t bytes) {
    mutex.lock();
    send_buffered(data, bytes);
    mutex.unlock();
}

void sender_t::send_immediate(uint8_t *data, size_t bytes) { // TODO: make send_rq = send_immediate(nullptr, 0)
    mutex.lock();
    send_buffered(data, bytes);
    send_raw(buf, buf_ptr);
    buf_ptr = 0;
    mutex.unlock();
}

void sender_t::send_raw(uint8_t *data, size_t bytes) {
    assert(bytes <= width);
    control->remote_get_md(data + bytes);
    socket.send(data, bytes + controller_t::md_size(), endpoint);
}

void sender_t::task_send(void *ctx) {
    auto *body = reinterpret_cast<sender_t *>(ctx);

    size_t bytes;

    while (true) {
        body->mutex.lock();

        bytes = body->send_cb(body->buf + body->buf_ptr, body->width - body->buf_ptr, body->client_data);
        assert(bytes <= body->width - body->buf_ptr);
        body->buf_ptr += bytes;

        body->send_raw(body->buf, body->buf_ptr);
        if (body->buf_ptr == body->width) body->buf_ptr = 0;

        body->mutex.unlock();
    }
}
