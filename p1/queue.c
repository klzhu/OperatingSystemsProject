/*
 * Generic queue implementation.
 *
 */
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

queue_t*
queue_new() {
    return NULL;
}

int
queue_prepend(queue_t *queue, void* item) {
    return -1;
}

int
queue_append(queue_t *queue, void* item) {
    return -1;
}

int
queue_dequeue(queue_t *queue, void** item) {
    return -1;
}

int
queue_iterate(queue_t *queue, func_t f, void* item) {
    return -1;
}

int
queue_free (queue_t *queue) {
    return -1;
}

int
queue_length(const queue_t *queue) {
    return -1;
}

int
queue_delete(queue_t *queue, void* item) {
    return -1;
}
