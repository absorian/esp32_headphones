//
// Created by ism on 01.06.2023.
//

#ifndef ESP_IMPL_H
#define ESP_IMPL_H

#include "esp_log.h"
#include "esp_event.h"
#include "asio.hpp"

#define logi(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#ifdef LOG_DEBUG
#define logd(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
#else
#define logd(tag, fmt, ...)
#endif
#define loge(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)

//
class esp_mutex {
    const char *TAG = "ESP_MUTEX";
public:
    esp_mutex() {
        mutex_impl = xSemaphoreCreateMutex();
        if (mutex_impl == nullptr) {
            loge(TAG, "Mutex was not created");
        }
    }

    inline void lock() {
        xSemaphoreTake(mutex_impl, portMAX_DELAY);
    }

    inline void unlock() {
        xSemaphoreGive(mutex_impl);
    }

private:
    xSemaphoreHandle mutex_impl;
};

typedef esp_mutex mutex_t;

//
#ifndef ESP_THREAD_STACK_DEPTH
#define ESP_THREAD_STACK_DEPTH 4096
#endif

#ifndef ESP_THREAD_PRIO
#define ESP_THREAD_PRIO tskIDLE_PRIORITY
#endif

class esp_thread {
public:
    typedef void (*function_t)(void *);

    explicit esp_thread(function_t function, void *argument = nullptr) {
        xTaskCreate(function, "", ESP_THREAD_STACK_DEPTH, argument,
                    ESP_THREAD_PRIO,
                    &handler);
        configASSERT(handler);
        vTaskSuspend(handler);
    }

    void launch() {
        vTaskResume(handler);
    }

    void wait() {
        vTaskSuspend(handler);
    }

    void terminate() {
        vTaskDelete(handler);
    }

private:
    TaskHandle_t handler = nullptr;
};

typedef esp_thread thread_t;

//
extern asio::io_context io_context_glob;

typedef asio::ip::address_v4 ip_address_t;
#define ip_addr_to_string(ip) ip.to_string()
#define ip_addr_from_string(str) asio::ip::make_address_v4(str)

typedef asio::ip::udp::endpoint udp_endpoint_t;

class asio_udp_socket {
    const char *TAG = "ASIO_UDP_SOCKET";
//    static asio::io_context io_context;
public:
    asio_udp_socket() : socket_impl(io_context_glob, asio::ip::udp::v4()) {}

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

typedef asio_udp_socket udp_socket_t;


#endif //ESP_IMPL_H
