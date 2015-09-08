/*
 * Generic queue implementation.
 *
 */
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct node{
void* elem;//value of current element or pointer to value of current element? rn its pointer
struct node * next;
}node;

typedef struct queue {
struct node* head;//first elem//struct Node?? that is t struct before node or not?
struct node* tail;//last elem
int length;//size
}queue;

queue_t* queue_new() {
	queue_t* q = malloc(sizeof(queue_t));
	//if memory is overcommitted
	if (q == NULL)
	{
		return NULL;
	}

	q->head=NULL;
	q->tail=NULL;
	q->length=0;
	return q;
}

int
queue_prepend(queue_t *queue, void* item) {
	node *newItem = malloc(sizeof(node));
	if (newItem == NULL || queue == NULL || item == NULL)
	{
		return -1;
	}

	newItem->elem = item;
	//if head is null, queue is empty
	if (queue->length == 0)
	{
		queue->head = newItem;
		queue->tail = newItem;
		queue->head->next = NULL;
	}
	//queue is not empty
	else
	{	
		newItem->next = queue->head;
		queue->head = newItem;
	}

	queue->length++;
	return 0;
}

int
queue_append(queue_t *queue, void* item) {
	node* newItem = malloc(sizeof(node));
	if (newItem == NULL || queue == NULL || item == NULL)
	{
		return -1;
	}

	newItem->elem = item;
	newItem->next = NULL;
	//if head is null, queue is empty
	if (queue->length == 0)
	{
		queue->head = newItem;
		queue->tail = newItem;
	}
	//queue is not empty
	else 
	{
		queue->tail->next = newItem;
		queue->tail = newItem;
	}

	queue->length++;
	return 0;
}

int
queue_dequeue(queue_t *queue, void** item) {
	//our queue is empty
	if (queue->length == 0 || queue == NULL)
	{
		*item = NULL;
		return -1;
	}
	//only 1 item in our queue
	else if (queue->length == 1)
	{
		*item = (queue->head->elem); //get the address of head node's element pointer
		free(queue->head);
		queue->head = NULL;
	}
	//more than 1 item in our queue
	else if (queue->length >1)
	{
		*item = (queue->head->elem); //get the address of head node's element pointer
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
	if (queue->length == 0 || queue == NULL || item == NULL)
	{
		return -1;
	}
	else
	{
		node* curr = queue->head;
		int i = 0;
	    for(; i<queue->length; i++)
	    {
			f(curr->elem, item);
			curr = curr->next;
	    }
	}	
    return 0;
}

int
queue_free (queue_t *queue) {  
	//if queue is empty or null
	if (queue->length == 0 || queue == NULL)
	{
		return -1;
	}
	else
	{
		node* curr = queue->head;
		queue->head = NULL;
		int i = 0;
	    for(; i<queue->length; i++)
	    {
			node* tempNext=curr->next;
	    	curr->next = NULL;
			free(curr);
			curr=tempNext;
	    }
		queue->length=0;
		free(queue);
		queue=NULL;
	}
	return 0;
}

int
queue_length(const queue_t *queue) {
	if (queue->length < 0 || queue == NULL)
	{
		return -1;
	}
	else
	{
		return queue->length;
	}
}

/*
 * Delete the first instance of the specified item from the given queue.
 * Returns 0 if an element was deleted, or -1 otherwise.
 */
int
queue_delete(queue_t *queue, void* item) {
	//if queue is empty
	if (queue->length == 0 || queue == NULL || item == NULL)
	{
		return -1;
	}
	//only 1 item in our queue
	else if (queue->length == 1)
	{
		//if our head contains item
		if (queue->head->elem == item)
		{
			free(queue->head);
			queue->head = NULL;
			return 0;
		}
	}
	else if (queue->length > 1)
	{
		//if our head contains the item
		if(queue->head->elem==item)
		{
        	node* next=queue->head->next;
            free(queue->head);
            queue->head=next;
            queue->length--;
			return 0;
		}
		else
		{
			node* prev=queue->head;
	        node* curr=prev;
	        int i = 0;
		    for(; i<queue->length; i++)
		    {
		    	curr = curr->next;
		    	if (curr->elem == item)
		    	{
		    		prev->next = curr->next;
		    		free(curr);
		    		curr = NULL;
		    		queue->length--;
		    		return 0;
		    	}
	    	}
      	}
  	}
return -1;
}
