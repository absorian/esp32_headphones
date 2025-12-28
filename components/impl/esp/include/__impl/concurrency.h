#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include <impl/concurrency.h>


typedef struct {
    SemaphoreHandle_t impl;
} semaphore_t;

typedef struct {
    SemaphoreHandle_t impl;
} mutex_t;

struct thread_t {
    TaskHandle_t impl = NULL;
    thread_func_t func;
    semaphore_t sem_exit;
    mutex_t mutex_exit;
};
