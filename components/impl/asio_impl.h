//
// Created by ism on 24.10.2023.
//

#ifndef ASIO_NET_H
#define ASIO_NET_H

#include <asio.hpp>
#include "impl.h"

typedef asio::ip::address_v4 ip_address_t;
typedef asio::ip::udp::endpoint udp_endpoint_t;

extern asio::io_context io_context_glob;

class asio_udp_socket_t {
    const char *TAG = "ASIO_UDP_SOCKET";
//    static asio::io_context io_context;
public:
    asio_udp_socket_t() : socket_impl(io_context_glob, asio::ip::udp::v4()) {}

    void bind(uint16_t port) {
        asio::error_code err;
        socket_impl.bind(udp_endpoint_t(asio::ip::udp::v4(), port), err);
        if (err) {
            loge(TAG, "Binding error: %s", err.message().c_str());
        }
    }

    void unbind() {
        if (socket_impl.is_open()) socket_impl.close();
        socket_impl.open(asio::ip::udp::v4());
    }

    void send(const uint8_t *data, size_t bytes, const udp_endpoint_t &endpoint) {
        socket_impl.send_to(asio::buffer(data, bytes), endpoint);
    }

    size_t receive(uint8_t *data, size_t max_bytes, udp_endpoint_t &endpoint) {
        return socket_impl.receive_from(asio::buffer(data, max_bytes), endpoint);
    }

private:
    asio::ip::udp::socket socket_impl;
};

typedef asio_udp_socket_t udp_socket_t;

#endif //ASIO_NET_H
