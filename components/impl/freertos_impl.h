//
// Created by ism on 01.06.2023.
//

#ifndef FREERTOS_IMPL_H
#define FREERTOS_IMPL_H

#include "esp_event.h"
#include <ctime>

class freertos_thread_t;

#define logi(tag, fmt, ...) printf(LOG_COLOR(LOG_COLOR_GREEN) "I (%lld) %s: " fmt LOG_RESET_COLOR "\n", freertos_thread_t::get_time_ms(), tag, ##__VA_ARGS__)
#define logp(tag, fmt, ...) printf(LOG_BOLD(LOG_COLOR_BLUE) "P %s: " fmt LOG_RESET_COLOR "\n", tag, ##__VA_ARGS__)
#define loge(tag, fmt, ...) printf(LOG_COLOR(LOG_COLOR_RED) "E (%lld) %s: " fmt LOG_RESET_COLOR "\n", freertos_thread_t::get_time_ms(), tag, ##__VA_ARGS__)
#define logr(fmt, ...) printf(fmt, ##__VA_ARGS__); fflush(stdout)

//
#ifndef ESP_THREAD_STACK_DEPTH
#define ESP_THREAD_STACK_DEPTH 4096
#endif

#ifndef ESP_THREAD_PRIO
#define ESP_THREAD_PRIO 5
#endif

// TODO: get rid of abstractions

class freertos_thread_t {
    static constexpr char TAG[] = "FREERTOS_THREAD";
public:
    typedef void (*function_t)(void *);

    explicit freertos_thread_t(function_t function, void *argument = nullptr, uint32_t prio = ESP_THREAD_PRIO,
                               uint32_t stack_size = ESP_THREAD_STACK_DEPTH) : function(function), argument(argument),
                                                                               prio(prio), stack_size(stack_size) {}

    ~freertos_thread_t() {
        terminate();
    }

    void launch() {
        if (!handler) {
            xTaskCreate(function, "freertos_thread_t", stack_size, argument, prio, &handler);
            configASSERT(handler);
        }
        vTaskResume(handler);
    }

    void wait() {
        loge(TAG, "Wait is unsupported!");
    }

    void suspend() {
        vTaskSuspend(handler);
    }

    void terminate() {
        vTaskDelete(handler);
    }

    static void sleep(uint32_t ms) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    static time_t get_time_ms() {
        struct timespec spec;
        clock_gettime(CLOCK_MONOTONIC, &spec);
        return spec.tv_sec * 1000L + spec.tv_nsec / (time_t) 1e6L;
    }

private:
    function_t function;
    void *argument;
    uint32_t prio;
    uint32_t stack_size;

    TaskHandle_t handler = nullptr;
};

typedef freertos_thread_t thread_t;

//
class freertos_mutex_t {
    static constexpr char TAG[] = "FREERTOS_MUTEX";
public:
    freertos_mutex_t() {
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

typedef freertos_mutex_t mutex_t;

#endif //FREERTOS_IMPL_H
