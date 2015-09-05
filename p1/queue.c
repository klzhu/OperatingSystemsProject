/*
 * Generic queue implementation.
 *
 */
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

 typedef struct Node{
void* elem;//value of current element or pointer to value of current element? rn its pointer
Node* next;
}Node;

typedef struct queue queue_t{
Node* head;//first elem//struct Node?? that is t struct before node or not?
Node* end;//last elem
int elems;//size
}queue_t;

queue_t* queue_new() {
	queue_t* q=(queue_t*)malloc(sizeof(queue_t));
	q->head=NULL;
	q->tail=NULL;
	q->elems=0;
	return q;
	/*WHEN would there be error?
    return NULL;*/
}

int
queue_prepend(queue_t *queue, void* item) {
	Node* temp=(Node *)malloc(sizeof( Node));
	temp->elem=item;
	temp->next=queue->head;
	queue->head=temp;
	/*when error?
    return -1;*/
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
