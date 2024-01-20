//
// Created by ism on 01.06.2023.
//

#ifndef ESP_IMPL_H
#define ESP_IMPL_H

#include "esp_log.h"
#include "esp_event.h"
#include "asio.hpp"

//
class freertos_mutex_t {
    const char *TAG = "FREERTOS_MUTEX";
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

//
#ifndef ESP_THREAD_STACK_DEPTH
#define ESP_THREAD_STACK_DEPTH 4096
#endif

#ifndef ESP_THREAD_PRIO
#define ESP_THREAD_PRIO 5
#endif

class freertos_thread_t {
    const char* TAG = "FREERTOS_THREAD";
public:
    typedef void (*function_t)(void *);

    explicit freertos_thread_t(function_t function, void *argument = nullptr) {
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

private:
    TaskHandle_t handler = nullptr;
};

typedef freertos_thread_t thread_t;

#endif //ESP_IMPL_H
