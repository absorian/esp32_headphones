#include <impl/concurrency.h>

#include <stdint.h>

static void *thread_runner(void *ctx) {
    thread_t *handle = ctx;
    handle->func.call(handle, handle->func.ctx);

    bin_sem_give(&handle->sem_exit);
    return NULL;
}

void thread_init(thread_t *handle, thread_func_t *func) {
    handle->func = *func;
    bin_sem_init(&handle->sem_exit);
    mutex_init(&handle->mutex_exit);
}

void thread_launch(thread_t *handle) {
    pthread_create(&handle->impl,
                   NULL,
                   thread_runner,
                   handle);
}

bool thread_exit_requested(thread_t *handle) {
    return bin_sem_locked(&handle->sem_exit);
}

void thread_exit(thread_t *handle) {
    mutex_lock(&handle->mutex_exit);
    bin_sem_wait(&handle->sem_exit);
    pthread_join(handle->impl, NULL);
    mutex_unlock(&handle->mutex_exit);
}


void bin_sem_init(semaphore_t *handle) {
    sem_init(&handle->impl, 0, 0);
}

void bin_sem_deinit(semaphore_t *handle) {
    sem_destroy(&handle->impl);
}

void bin_sem_wait(semaphore_t *handle) {
    sem_wait(&handle->impl);
}

int bin_sem_trywait(semaphore_t *handle, time_t ms) {
    uint32_t ms_nosec = ms % 1000L;
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    spec.tv_sec += (ms - ms_nosec) / 1000L;
    spec.tv_nsec += (long) (ms_nosec * 1e6L);
    return sem_timedwait(&handle->impl, &spec); // ret -1 on fail
}

void bin_sem_give(semaphore_t *handle) {
    // OLD: something breaks when this is added.
    // if (!bin_sem_locked(handle)) return;
    sem_post(&handle->impl);
}

bool bin_sem_locked(semaphore_t *handle) {
    int val;
    sem_getvalue(&handle->impl, &val);
    return val <= 0;
}


void mutex_init(mutex_t *handle) {
    pthread_mutex_init(&handle->impl, NULL);
}

void mutex_deinit(mutex_t *handle) {
    pthread_mutex_destroy(&handle->impl);
}

void mutex_lock(mutex_t *handle) {
    pthread_mutex_lock(&handle->impl);
}

void mutex_unlock(mutex_t *handle) {
    pthread_mutex_unlock(&handle->impl);
}
