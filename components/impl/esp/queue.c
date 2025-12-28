#include <impl/queue.h>

#include <impl/concurrency.h>
#include <impl/log.h>
#include <impl/time.h>

#include <freertos/FreeRTOSConfig.h>
#include <freertos/queue.h>
#include <portmacro.h>
#include <freertos/projdefs.h>

int queue_init(queue_t *q, int max_len, int item_sz) {
    q->impl = xQueueCreate(max_len, item_sz);
    if (q->impl == NULL) {
        loge("Queue init failed");
        return -1;
    }
    return 0;
}

int queue_enqueue(queue_t *q, void *data) {
    xQueueSend(q->impl, data, portMAX_DELAY);
    return 0;
}


int queue_dequeue(queue_t *q, void *data, time_t timeout) {
    return xQueueReceive(q->impl, data, pdUS_TO_TICKS(timeout)) == pdTRUE ? 0 : -1;
}

void queue_destroy(queue_t *q) {
    vQueueDelete(q->impl);
}
