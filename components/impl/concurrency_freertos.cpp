#include <impl/concurrency.h>
#include <impl/log.h>

#include <cstring>

static const char* TAG = "CONC_FREERTOS";

void
thread_init(thread_t *handle, ctx_func_t<thread_func_t> func, const char *name, uint32_t prio, uint32_t stack_size) {
    handle->function = func;
    strcpy(handle->name, name);
    handle->prio = prio;
    handle->stack_size = stack_size;
}

void thread_launch(thread_t *handle) {
    if (!handle->handle) {
        xTaskCreate(handle->function.function(), handle->name, handle->stack_size, handle->function.context(), handle->prio, &handle->handle);
        configASSERT(handle->handle);
    }
    vTaskResume(handle->handle);
}

void thread_wait(thread_t *handle) {
    loge(TAG, "Wait is not supported!");
}

void thread_terminate(thread_t *handle) {
    if (!handle->handle) return;
    vTaskDelete(handle->handle);
    handle->handle = nullptr;
}

void thread_suspend(thread_t *handle) {
    vTaskSuspend(handle->handle);
}

time_t thread_millis() {
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return spec.tv_sec * 1000L + spec.tv_nsec / (time_t) 1e6L;
}

void thread_sleep(time_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}


void bin_sem_init(semaphore_t *handle) {
    handle->handle = xSemaphoreCreateBinary();
    if (handle->handle == nullptr) {
        loge(TAG, "Binary semaphore init error");
    }
}

void bin_sem_deinit(semaphore_t *handle) {
    vSemaphoreDelete(handle->handle);
}

int bin_sem_take(semaphore_t *handle, time_t ms) {
    if (!ms) {
        xSemaphoreTake(handle->handle, portMAX_DELAY);
        return 0;
    }
    if (xSemaphoreTake(handle->handle, pdMS_TO_TICKS(ms))) return 0;
    return -1;
}

void bin_sem_give(semaphore_t *handle) {
    xSemaphoreGive(handle->handle);
}

bool bin_sem_taken(semaphore_t *handle) {
    return !uxSemaphoreGetCount(handle->handle);
}


void mutex_init(mutex_t *handle) {
    handle->handle = xSemaphoreCreateMutex();
    if (handle->handle == nullptr) {
        loge(TAG, "Mutex init error");
    }
}

void mutex_deinit(mutex_t *handle) {
    vSemaphoreDelete(handle->handle);
}

void mutex_lock(mutex_t *handle) {
    xSemaphoreTake(handle->handle, portMAX_DELAY);
}

void mutex_unlock(mutex_t *handle) {
    xSemaphoreGive(handle->handle);
}

