#pragma once

#include <pthread.h>
#include <semaphore.h>

#include <impl/concurrency.h>

typedef struct {
    sem_t impl;
} semaphore_t;

typedef struct {
    pthread_mutex_t impl;
} mutex_t;

struct thread_t {
    pthread_t impl = 0;
    thread_func_t func;
    semaphore_t sem_exit;
    mutex_t mutex_exit;
};
