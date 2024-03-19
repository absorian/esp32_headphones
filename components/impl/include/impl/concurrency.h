#ifndef IMPL_CONCURRENCY_H
#define IMPL_CONCURRENCY_H

#include <ctime>
#include <cstdint>

#include "helpers.h"

#ifndef ESP_THREAD_STACK_DEPTH
#define ESP_THREAD_STACK_DEPTH 4096
#endif

#ifndef ESP_THREAD_PRIO
#define ESP_THREAD_PRIO 5
#endif

#define ESP_THREAD_DEFAULT_NAME "concurrent_task"

#ifdef ESP_PLATFORM
#include <__impl/concurrency_freertos.h>
#else
#include <__impl/concurrency_posix.h>
#endif

typedef struct thread_t thread_t;
typedef struct semaphore_t semaphore_t;
typedef struct mutex_t mutex_t;


void thread_init(thread_t *handle, ctx_func_t<thread_func_t> func, const char *name = ESP_THREAD_DEFAULT_NAME,
                 uint32_t prio = ESP_THREAD_PRIO,
                 uint32_t stack_size = ESP_THREAD_STACK_DEPTH);

void thread_launch(thread_t *handle);

void thread_wait(thread_t *handle);

void thread_terminate(thread_t *handle);

void thread_suspend(thread_t *handle);

time_t thread_millis();

void thread_sleep(time_t ms);


void bin_sem_init(semaphore_t *handle);

void bin_sem_deinit(semaphore_t *handle);

int bin_sem_take(semaphore_t *handle, time_t timeout_ms = 0);

void bin_sem_give(semaphore_t *handle);

bool bin_sem_taken(semaphore_t *handle);


void mutex_init(mutex_t *handle);

void mutex_deinit(mutex_t *handle);

void mutex_lock(mutex_t *handle);

void mutex_unlock(mutex_t *handle);

#endif //IMPL_CONCURRENCY_H
