//
// Created by ism on 01.06.2023.
//

#ifndef SFML_IMPL_H
#define SFML_IMPL_H

#include <SFML/System.hpp>
#include <SFML/Network.hpp>
#include <string>


#define logi(tag, fmt, ...) printf((std::string("%s: ") + fmt + std::string("\n")).c_str(), tag, ##__VA_ARGS__)

#ifdef LOG_DEBUG
#define logd(tag, fmt, ...) printf((std::string("%s: ") + fmt + std::string("\n")).c_str(), tag, ##__VA_ARGS__)
#else
#define logd(tag, fmt, ...)
#endif

#define loge(tag, fmt, ...) fprintf(stderr, (std::string("%s: ") + fmt + std::string("\n")).c_str(), tag, ##__VA_ARGS__)

//
typedef sf::Mutex mutex_t;

//
class sfml_thread : public sf::Thread {
public:
    typedef void (*function_t)(void *);

    explicit sfml_thread(function_t function, void *argument = nullptr) : sf::Thread(function, argument) {}
};

typedef sfml_thread thread_t;

//
typedef sf::IpAddress ip_address_t;
#define ip_addr_to_string(ip) ip.toString()
#define ip_addr_from_string(str) ip_address_t(str)

//
class sfml_udp_endpoint {
public:
    sfml_udp_endpoint() = default;

    sfml_udp_endpoint(const ip_address_t& address, uint16_t port) : m_addr(address), m_port(port) {}

    uint16_t port() const {
        return m_port;
    }

    void port(uint16_t port_num) {
        m_port = port_num;
    }

    ip_address_t address() const {
        return m_addr;
    }

    void address(const ip_address_t &addr) {
        m_addr = addr;
    }

    inline bool operator==(const sfml_udp_endpoint &rhs) const {
        return m_addr == rhs.m_addr &&
               m_port == rhs.m_port;
    }

    inline bool operator!=(const sfml_udp_endpoint &rhs) const {
        return !(rhs == *this);
    }

private:
    ip_address_t m_addr;
    uint16_t m_port{};
};

typedef sfml_udp_endpoint udp_endpoint_t;

//
class sfml_udp_socket {
    const char *TAG = "ASIO_UDP_SOCKET";
public:
    sfml_udp_socket() = default;

    void bind(uint16_t port) {
        sf::Socket::Status err = socket_impl.bind(port);
        if (err != sf::Socket::Done) {
            loge(TAG, "Binding error: Status %s", err);
        }
    }

    void unbind() {
        socket_impl.unbind();
    }

    void send(const uint8_t *data, size_t bytes, const udp_endpoint_t &endpoint) {
        socket_impl.send(data, bytes, endpoint.address(), endpoint.port());
    }

    size_t receive(uint8_t *data, size_t max_bytes, udp_endpoint_t &endpoint) {
        socket_impl.receive(data, max_bytes, received, recv_ip, recv_port);
        endpoint.address(recv_ip);
        endpoint.port(recv_port);
        return received;
    }

private:
    sf::UdpSocket socket_impl;

    size_t received{};
    ip_address_t recv_ip;
    uint16_t recv_port{};
};

typedef sfml_udp_socket udp_socket_t;

#endif //SFML_IMPL_H
