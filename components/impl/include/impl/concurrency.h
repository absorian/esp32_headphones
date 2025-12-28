#pragma once

#include <time.h>
#include <stdbool.h>

typedef struct thread_t thread_t;

typedef struct {
    void *ctx = NULL;

    void (*call)(thread_t *, void *) = NULL;
} thread_func_t;

#include <__impl/concurrency.h>

void thread_init(thread_t *handle, thread_func_t *func);

void thread_launch(thread_t *handle);

bool thread_exit_requested(thread_t *handle);

void thread_exit(thread_t *handle);


void bin_sem_init(semaphore_t *handle);

void bin_sem_deinit(semaphore_t *handle);

void bin_sem_wait(semaphore_t *handle);

int bin_sem_trywait(semaphore_t *handle, time_t us);

void bin_sem_give(semaphore_t *handle);

bool bin_sem_locked(semaphore_t *handle);


void mutex_init(mutex_t *handle);

void mutex_deinit(mutex_t *handle);

void mutex_lock(mutex_t *handle);

void mutex_unlock(mutex_t *handle);
