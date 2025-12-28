#pragma once

#include <pthread.h>

typedef struct queue_node {
    void *data;
    struct queue_node *next;
} queue_node_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    queue_node_t *head;
    queue_node_t *tail;
    int shutdown;
    size_t size;
    int item_sz;
} queue_t;

