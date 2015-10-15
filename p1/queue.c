/*
* Generic queue implementation.
*
*/
// #define NDEBUG //REENABLE BEFORE SUBMITTING
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include "queue.h"

typedef struct node {
	void* itemPtr;//pointer to node's item
	struct node* next;
	uint64_t order; //priority of the node, lower order means it should be closer to the head. Non priority queue usage will ignore this.
}node_t;

struct queue {
	node_t* head;//ptr to first node
	node_t* tail;//ptr to last node
	int length;//size
};

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
	node_t *newItem = malloc(sizeof(node_t));
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

	node_t* newItem = malloc(sizeof(node_t));
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
	//validate our inputs and ensure queue is not empty
	if (item == NULL) return -1; 
	if (queue->length == 0 || queue == NULL)
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
		node_t *newHead = queue->head->next;
		free(queue->head);
		queue->head = newHead;
	}

	queue->length--;
	return 0;
}

/*
* Returns the first element of the queue
* Returns 0 if successful, -1 otherwise
*/
int queue_peek(queue_t* queue, void** item) {
	//validate inputs and ensure queue is not empty
	if (item == NULL) return -1;
	if (queue == NULL || queue->length == 0) {
		*item = NULL;
		return -1;
	}

	assert(queue->head != NULL); //since queue is not empty, head should not be null

	*item = queue->head->itemPtr; //return the item pointer that the head is pointing to
	return 0;
}

int
queue_iterate(queue_t *queue, func_t f, void* item) {
	//if queue is empty or null
	if (queue == NULL || f == NULL) return -1;

	node_t* curr = queue->head;
	while (curr != NULL)
	{
		f(curr->itemPtr, item);
		curr = curr->next;
	}
	return 0;
}

//this method only frees the queue and assumes the nodes have already been freed (P2 spec)
int
queue_free(queue_t *queue) {
	//if queue is null or non empty, return -1
	if (queue == NULL || queue->length != 0) return -1;
	
	//otherwise, free queue and return 0
	free(queue);
	return 0;
}

//this method frees all the nodes as well as the queue itself (P1 and P3 spec)
int
queue_free_nodes_and_queue(queue_t *queue) {
	//if queue is empty or null
	if (queue == NULL) return 0;

	node_t* curr = queue->head;
	while (curr != NULL)
	{
		node_t* tempNext = curr->next;
		free(curr);
		curr = tempNext;
	}

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

	node_t *prev = NULL;
	node_t *curr = queue->head;
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

int 
queue_ordered_insert(queue_t* queue, void* item, uint64_t orderVal) {
	if (queue == NULL || item == NULL) return -1;

	node_t* newItem = malloc(sizeof(node_t));
	if (newItem == NULL) return -1;

	newItem->itemPtr = item;
	newItem->order = orderVal;

	node_t *prev = NULL;
	node_t *curr = queue->head;
	// Traverse the queue such that newItem is ordered between prev and curr: prev->newItem->curr
	while (curr != NULL && newItem->order > curr->order) //while the current node's priority is higher than ours and we haven't reached the end of the queue yet
	{
		prev = curr;
		curr = curr->next;
	}

	// By now, the order should be prev->newItem->curr, but we need to deal with NULL cases
	if (prev == NULL) queue->head = newItem;	//if prev is NULL, our original queue was empty and our new item is first item
	else prev->next = newItem; //otherwise, prev should now point to the new item

	newItem->next = curr;
	if (curr == NULL) 	queue->tail = newItem;	 //if curr is NULL, we reached end of list and new item is now new tail

	queue->length++;
	return 0;
}

