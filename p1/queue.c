/*
 * Generic queue implementation.
 *
 */
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

 typedef struct Node{
void* elem;//value of current element or pointer to value of current element? rn its pointer
struct Node * next;
}Node;

 typedef struct queue {
struct Node* head;//first elem//struct Node?? that is t struct before node or not?
struct Node* tail;//last elem
int length;//size
}queue;/**/






queue_t* queue_new() {
	queue_t* q=(queue_t*)malloc(sizeof(queue_t));
	q->head=NULL;
	q->tail=NULL;
	q->elems=0;
	return q;/**/
	/*WHEN would there be error?
    return NULL;*/
    return NULL;
}

int
queue_prepend(queue_t *queue, void* item) {
	struct Node* temp=(struct  Node *)malloc(sizeof( struct  Node));
	temp->elem=item;
	temp->next=queue->head;
	queue->head=temp;/**/
	return 0;
	/*when error?
    return -1;*/
}

int
queue_append(queue_t *queue, void* item) {
	Node* newItem = malloc(sizeof(Node));
	newItem->elem = item;
	newItem->next = NULL;
	queue->tail->next = newItem;
	queue->tail = newItem;
	queue->elems++;

	return 0;//success, return -1 if error
}

int
queue_dequeue(queue_t *queue, void** item) {
	item=&(queue->head->elem);
	struct Node* tempNext=queue->head->next;
	free(queue->head);
	queue->head=tempNext;/**/
	return 0;
	/*
    return -1;*/
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
	return queue->length;
   /* return -1;*/
}

int
queue_delete(queue_t *queue, void* item) {
    return -1;
}
