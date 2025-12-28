#include <impl/queue.h>
#include <impl/time.h>

#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

// TODO: use malloc at init only
// or check out posix message queues
int queue_init(queue_t *q, int max_len, int item_sz) {
    if (!q) return -1;

    q->head = q->tail = NULL;
    q->shutdown = 0;
    q->size = 0;
    q->item_sz = item_sz;

    if (pthread_mutex_init(&q->lock, NULL) != 0)
        return -1;
    if (pthread_cond_init(&q->cond, NULL) != 0) {
        pthread_mutex_destroy(&q->lock);
        return -1;
    }
    return 0;
}

int queue_enqueue(queue_t *q, void *data) {
    if (!q) return -1;

    queue_node_t *new_node = malloc(sizeof(queue_node_t));
    if (!new_node) return -1;

    new_node->data = malloc(q->item_sz);
    memcpy(new_node->data, data, q->item_sz);
    new_node->next = NULL;

    pthread_mutex_lock(&q->lock);

    if (q->shutdown) {
        pthread_mutex_unlock(&q->lock);
        free(new_node->data);
        free(new_node);
        return -1;
    }

    if (!q->tail) {
        q->head = q->tail = new_node;
    } else {
        q->tail->next = new_node;
        q->tail = new_node;
    }
    q->size++;

    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

int queue_dequeue(queue_t *q, void *data, time_t timeout) {
    if (!q) return -1;

    pthread_mutex_lock(&q->lock);

    struct timespec t = time_timespec_from_us(timeout);
    while (!q->head && !q->shutdown) {
        if (pthread_cond_timedwait(&q->cond, &q->lock, &t)) {
            pthread_mutex_unlock(&q->lock);
            return -1;
        }
    }

    if (q->shutdown && !q->head) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }

    queue_node_t *old_head = q->head;
    memcpy(data, old_head->data, q->item_sz);

    q->head = old_head->next;
    if (!q->head) {
        q->tail = NULL;
    }
    q->size--;

    pthread_mutex_unlock(&q->lock);
    free(old_head->data);
    free(old_head);
    return 0;
}

void queue_deinit(queue_t *q) {
    if (!q) return;

    pthread_mutex_lock(&q->lock);
    q->shutdown = 1;
    pthread_cond_broadcast(&q->cond);

    queue_node_t *current = q->head;
    while (current) {
        queue_node_t *next = current->next;
        free(current->data);
        free(current);
        current = next;
    }

    q->head = q->tail = NULL;
    q->size = 0;
    pthread_mutex_unlock(&q->lock);

    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->cond);
}
