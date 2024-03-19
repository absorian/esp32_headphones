#ifndef CONCURRENCY_POSIX_H
#define CONCURRENCY_POSIX_H

#include <pthread.h>
#include <semaphore.h>

typedef void (*thread_func_t)(void *);
struct thread_t {
    pthread_t handle = 0;
    ctx_func_t<thread_func_t> function;
};

struct semaphore_t {
    sem_t handle = nullptr;
};

struct mutex_t {
    pthread_mutex_t handle = 0;
};


#endif //CONCURRENCY_POSIX_H
