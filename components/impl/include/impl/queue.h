#pragma once

#include <__impl/queue.h>

#include <time.h>

int queue_init(queue_t *q, int max_len, int item_sz);

int queue_enqueue(queue_t *q, void *data);

int queue_dequeue(queue_t *q, void *data, time_t timeout);

void queue_deinit(queue_t *q);
