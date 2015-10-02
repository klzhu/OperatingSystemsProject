/*
* Generic queue implementation.
*
*/
#define NDEBUG 
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "alarm.h"

typedef struct node {
	void* itemPtr;//pointer to node's item
	struct node* next;
}node;

//node for our priority sorted queue
typedef struct priorityNode {
	void* itemPtr; //pointer to node's item
	struct pNode* next;
	int sortingVal; //value that our priority queue will be sorted on 
}pNode;

typedef struct queue {
	node* head;//ptr to first node
	node* tail;//ptr to last node
	int length;//size
}queue;

queue_t* queue_new() {
	queue_t* q = malloc(sizeof(queue_t));
	//if memory is overcommitted
	if (q == NULL) return NULL;

	q->head = NULL;
	q->tail = NULL;
	q->length = 0;
	return q;
}

int
queue_prepend(queue_t *queue, void* item) {
	if (queue == NULL || item == NULL) return -1;

	//if malloc returns NULL
	node *newItem = malloc(sizeof(node));
	if (newItem == NULL) return -1;

	newItem->itemPtr = item;
	newItem->next = queue->head;
	queue->head = newItem;
	//if head is null, queue is empty
	if (queue->length == 0)
	{
		queue->tail = newItem;
	}

	queue->length++;
	return 0;
}

int
queue_append(queue_t *queue, void* item) {
	if (queue == NULL || item == NULL) return -1;

	node* newItem = malloc(sizeof(node));
	if (newItem == NULL) return -1;

	newItem->itemPtr = item;
	newItem->next = NULL;
	//if head is null, queue is empty
	if (queue->length == 0)
	{
		queue->head = newItem;
	}
	else //queue is not empty
	{
		queue->tail->next = newItem;
	}

	queue->tail = newItem;
	queue->length++;

	return 0;
}

int
queue_dequeue(queue_t *queue, void** item) {
	//our queue is empty, if our queue is NULL, or the item is NULL, return -1
	if (queue->length == 0 || queue == NULL || item == NULL)
	{
		*item = NULL;
		return -1;
	}

	assert(queue->head != NULL && queue->tail != NULL);

	//only 1 item in our queue
	if (queue->length == 1)
	{
		*item = (queue->head->itemPtr); //get the address of head node's element pointer
		free(queue->head);
		queue->head = NULL;
		queue->tail = NULL;
	}
	else if (queue->length >1) //more than 1 item in our queue
	{
		*item = (queue->head->itemPtr); //get the address of head node's element pointer
		node *newHead = queue->head->next;
		free(queue->head);
		queue->head = newHead;
	}

	queue->length--;
	return 0;
}

int
queue_iterate(queue_t *queue, func_t f, void* item) {
	//if queue is empty or null
	if (queue == NULL || f == NULL) return -1;

	node* curr = queue->head;
	while (curr != NULL)
	{
		f(curr->itemPtr, item);
		curr = curr->next;
	}
	return 0;
}

int
queue_free(queue_t *queue) {
	//if queue is null or non empty, return -1
	if (queue == NULL || queue->length != 0) return -1;
	
	//otherwise, free queue and return 0
	free(queue);
	return 0;
}

int
queue_length(const queue_t *queue) {
	if (queue == NULL) return -1;

	assert(queue->length >= 0);
	return queue->length;
}

/*
* Delete the first instance of the specified item from the given queue.
* Returns 0 if an element was deleted, or -1 otherwise.
*/
int
queue_delete(queue_t *queue, void* item) {
	if (queue == NULL || item == NULL) return -1;

	node *prev = NULL;
	node *curr = queue->head;
	while (curr != NULL && curr->itemPtr != item)
	{
		prev = curr;
		curr = curr->next;
	}

	if (curr == NULL) return -1; //not found
	else //curr holds item
	{
		if (prev == NULL) //the head contains the item
		{
			queue->head = curr->next;
		}
		else
		{
			prev->next = curr->next;
		}
		
		if (curr->next == NULL) //tail contains item
		{
			queue->tail = prev;
		}

		free(curr);
		queue->length--;
		return 0;
	}
}

/*
* Delete the first instance of the specified item from the given queue.
* Returns 0 if an element was found, or -1 and NULL otherwise.
*/
int
queue_search(queue_t *queue, void* itemToFind, void** itemToReturn) {
	if (queue == NULL || itemToFind == NULL) return -1;

	node *prev = NULL;
	node *curr = queue->head;
	while (curr != NULL && curr->itemPtr != itemToFind)
	{
		prev = curr;
		curr = curr->next;
	}

	if (curr == NULL) return -1; //not found
	else //curr holds item
	{
		*itemToReturn = curr;
	}
	return 0;
}

/*
* Add item to the queue so that the queue maintains a sorted order
* Returns 0 if an element was added, or -1 otherwise.
*/
int 
queue_insertionsort(queue_t* queue, alarm_id item) {

}
