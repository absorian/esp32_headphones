//
// Created by ism on 01.06.2023.
//

#include <algorithm>
#include "sender.h"

sender_t::sender_t(controller_t *controller, size_t pipe_width) : control(controller),
                                                                  buf(pipe_width + controller_t::md_size()),
                                                                  width(pipe_width) {}

void sender_t::set_endpoint(const udp_endpoint_t &enp) {
    endpoint = enp;
}

void sender_t::send(uint8_t *data, size_t bytes) {
    while (buf_ptr + bytes >= width) {
        std::copy(data, data + width - buf_ptr, buf.begin() + buf_ptr);
        control->remote_get_md(buf.data() + width);
        send_raw(buf.data(), width + controller_t::md_size());
        data += width - buf_ptr;
        bytes -= width - buf_ptr;
        buf_ptr = 0;
    }
    std::copy(data, data + bytes, buf.data() + buf_ptr);
    buf_ptr += bytes;
}

void sender_t::send_immediate(uint8_t *data, size_t bytes) {
    while (buf_ptr + bytes >= width) {
        std::copy(data, data + width - buf_ptr, buf.data() + buf_ptr);
        control->remote_get_md(buf.data() + width);
        send_raw(buf.data(), width + controller_t::md_size());
        data += width - buf_ptr;
        bytes -= width - buf_ptr;
        buf_ptr = 0;
    }
    std::copy(data, data + bytes, buf.data() + buf_ptr);
    control->remote_get_md(buf.data() + buf_ptr + bytes);
    send_raw(buf.data(), buf_ptr + bytes + controller_t::md_size());
    buf_ptr = 0;
}

void sender_t::send_raw(uint8_t *data, size_t bytes) {
    socket.send(data, bytes, endpoint);
}
