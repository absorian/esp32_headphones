#ifndef CONCURRENCY_FREERTOS_H
#define CONCURRENCY_FREERTOS_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

typedef void (*thread_func_t)(void *);
typedef struct thread_t {
    TaskHandle_t handle = nullptr;

    ctx_func_t<thread_func_t> function;
    uint32_t prio;
    uint32_t stack_size;
    char name[64];
} thread_t;

typedef struct semaphore_t {
    SemaphoreHandle_t handle;
} semaphore_t;

struct mutex_t {
    SemaphoreHandle_t handle;
};


#endif //CONCURRENCY_FREERTOS_H
