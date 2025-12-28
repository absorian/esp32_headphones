#include <impl/concurrency.h>
#include <impl/time.h>
#include <impl/log.h>

#include <string.h>

static void thread_runner(void *ctx) {
    thread_t *handle = ctx;
    handle->func.call(handle, handle->func.ctx);

    bin_sem_give(&handle->sem_exit);
    vTaskDelete(NULL);
}

void thread_init(thread_t *handle, thread_func_t *func) {
    handle->func = *func;
    handle->impl = NULL;
    bin_sem_init(&handle->sem_exit);
    mutex_init(&handle->mutex_exit);
}

void thread_launch(thread_t *handle) {
    if (handle->impl) {
        logw("Tried to launch an existing thread");
    }

    xTaskCreate(thread_runner,
                "freertos_thread",
                4096, handle, 5,
                &handle->impl);
    configASSERT(handle->impl);
}

bool thread_exit_requested(thread_t *handle) {
    return bin_sem_locked(&handle->sem_exit);
}

void thread_exit(thread_t *handle) {
    mutex_lock(&handle->mutex_exit);
    bin_sem_wait(&handle->sem_exit);
    handle->impl = NULL;
    mutex_unlock(&handle->mutex_exit);
}

void bin_sem_init(semaphore_t *handle) {
    handle->impl = xSemaphoreCreateBinary();
    if (handle->impl == NULL) {
        loge("Binary semaphore init failed");
    }
}

void bin_sem_deinit(semaphore_t *handle) {
    vSemaphoreDelete(handle->impl);
}

void bin_sem_wait(semaphore_t *handle) {
    xSemaphoreTake(handle->impl, portMAX_DELAY);
}

int bin_sem_trywait(semaphore_t *handle, time_t us) {
    return xSemaphoreTake(handle->impl, pdUS_TO_TICKS(us)) ? 0 : -1;
}

void bin_sem_give(semaphore_t *handle) {
    xSemaphoreGive(handle->impl);
}

bool bin_sem_locked(semaphore_t *handle) {
    return !uxSemaphoreGetCount(handle->impl);
}


void mutex_init(mutex_t *handle) {
    handle->impl = xSemaphoreCreateMutex();
    if (handle->impl == NULL) {
        loge("Mutex init error");
    }
}

void mutex_deinit(mutex_t *handle) {
    vSemaphoreDelete(handle->impl);
}

void mutex_lock(mutex_t *handle) {
    xSemaphoreTake(handle->impl, portMAX_DELAY);
}

void mutex_unlock(mutex_t *handle) {
    xSemaphoreGive(handle->impl);
}
