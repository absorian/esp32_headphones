//
// Created by ism on 01.06.2023.
//

#ifndef POSIX_IMPL_H
#define POSIX_IMPL_H

#include <string>
#include <pthread.h>


//
class p_thread_t {
    static constexpr char TAG[] = "PTHREAD";
public:
    typedef void (*function_t)(void *);

    explicit p_thread_t(function_t function, void *argument = nullptr) : func(function), func_arg(argument) {}

    void launch() {
        pthread_create(&handler, nullptr, reinterpret_cast<void *(*)(void *)>(func), func_arg);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
        pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);
    }

    void wait() {
        pthread_join(handler, nullptr);
    }

    void suspend() {
        loge(TAG, "Suspend is unsupported!");
    }

    void terminate() {
        pthread_cancel(handler);
//        pthread_join(handler, nullptr); // TODO
    }

    static void sleep(uint32_t ms) {
        const timespec t = {static_cast<long>(ms / 1000), static_cast<long>((ms % 1000) * 1000)};
        nanosleep(&t, nullptr);
    }

    static time_t get_time_ms() {
        struct timespec spec;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &spec);
        return spec.tv_sec * 1000L + spec.tv_nsec / 1e6L;
    }

private:
    pthread_t handler = 0;
    function_t func;
    void* func_arg;
};

typedef p_thread_t thread_t;

//
class p_thread_mutex_t {
    static constexpr char TAG[] = "PTHREAD_MUTEX";
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
    pthread_mutex_t mutex_impl{};
};

typedef p_thread_mutex_t mutex_t;

#endif //POSIX_IMPL_H
