#include <impl/concurrency.h>
#include <impl/log.h>

static const char* TAG = "CONC_PTHREAD";

void
thread_init(thread_t *handle, ctx_func_t<thread_func_t> func, const char *name, uint32_t prio, uint32_t stack_size) {
    handle->function = func;
}

void thread_launch(thread_t *handle) {
    // actually, priority and stack size can be configured through pthread_attr_t
    pthread_create(&handle->handle, nullptr, reinterpret_cast<void *(*)(void *)>(handle->function.function()),
                   handle->function.context());
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);
}

void thread_wait(thread_t *handle) {
    pthread_join(handle->handle, nullptr);
}

void thread_terminate(thread_t *handle) {
    // not working properly
    pthread_cancel(handle->handle);
}

void thread_suspend(thread_t *handle) {
    loge(TAG, "Suspend is not supported!");
}

time_t thread_millis() {
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return spec.tv_sec * 1000L + spec.tv_nsec / 1e6L;
}

void thread_sleep(time_t ms) {
    const timespec t = {static_cast<long>(ms / 1000), static_cast<long>((ms % 1000) * 1000)};
    nanosleep(&t, nullptr);
}


void bin_sem_init(semaphore_t *handle) {
    sem_init(&handle->handle, 0, 0);
}

void bin_sem_deinit(semaphore_t *handle) {
    sem_destroy(&handle->handle);
}

int bin_sem_take(semaphore_t *handle, time_t ms) {
    if (!ms) {
        sem_wait(&handle->handle);
        return 0;
    }
    uint32_t ms_nosec = ms % 1000L;
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    spec.tv_sec += (ms - ms_nosec) / 1000L;
    spec.tv_nsec += static_cast<long>(ms_nosec * 1e6L);
    return sem_timedwait(&handle->handle, &spec); // ret -1 on fail
}

void bin_sem_give(semaphore_t *handle) {
//    if (!bin_sem_taken(handle)) return; // something breaks when this added
    // can be interrupted here, what to do
    sem_post(&handle->handle);
}

bool bin_sem_taken(semaphore_t *handle) {
    int val;
    sem_getvalue(&handle->handle, &val);
    return val <= 0;
}


void mutex_init(mutex_t *handle) {
    pthread_mutex_init(&handle->handle, nullptr);
}

void mutex_deinit(mutex_t *handle) {
    pthread_mutex_destroy(&handle->handle);
}

void mutex_lock(mutex_t *handle) {
    pthread_mutex_lock(&handle->handle);
}

void mutex_unlock(mutex_t *handle) {
    pthread_mutex_unlock(&handle->handle);
}

