//
// Created by ism on 01.06.2023.
//

#ifndef SFML_IMPL_H
#define SFML_IMPL_H

#include <string>
#include <pthread.h>
#include <unistd.h>

//
class p_thread_mutex_t {
    const char *TAG = "ESP_MUTEX";
public:
    p_thread_mutex_t() {
        pthread_mutex_init(&mutex_impl, nullptr);
    }

    inline void lock() {
        pthread_mutex_lock(&mutex_impl);
    }

    inline void unlock() {
        pthread_mutex_unlock(&mutex_impl);
    }

private:
    pthread_mutex_t mutex_impl;
};

typedef p_thread_mutex_t mutex_t;

//
class p_thread_t {
    const char* TAG = "PTHREAD";
public:
    typedef void (*function_t)(void *);

    explicit p_thread_t(function_t function, void *argument = nullptr) : func(function), func_arg(argument) {}

    void launch() {
        // see if the cast works
        // it works!!
        pthread_create(&handler, nullptr, reinterpret_cast<void *(*)(void *)>(func), func_arg);
    }

    void wait() {
        pthread_join(handler, nullptr);
    }

    void suspend() {
        loge(TAG, "Suspend is unsupported!");
    }

    void terminate() {
        pthread_cancel(handler);
    }

    static void sleep(uint32_t ms) {
        const timespec t = {static_cast<long>(ms / 1000), static_cast<long>((ms % 1000) * 1000)};
        nanosleep(&t, nullptr);
    }

private:
    pthread_t handler = 0;
    function_t func;
    void* func_arg;
};

typedef p_thread_t thread_t;

#endif //SFML_IMPL_H
