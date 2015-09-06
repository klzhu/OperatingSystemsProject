/*
 * Generic queue implementation.
 *
 */
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

void testAppend(queue_t * queue)
{
	
}

 int main(){
	//test that we're constructing a new queue with a null head ptr, null tail ptr, and 0 elems
 	queue_t* q=queue_new();
	assert(q->head == NULL);
	assert(q->tail == NULL);
	assert(q->length = 0);
	
	
 	printf("%d \n", queue_length(q));
 	return 1;
 }