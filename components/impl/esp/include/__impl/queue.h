#pragma once

#include <freertos/queue.h>

typedef struct queue {
    QueueHandle_t impl;
} queue_t;
