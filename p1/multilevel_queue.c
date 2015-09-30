/*
 * Multilevel queue manipulation functions  
 */
#include "multilevel_queue.h"
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

/* multilevel queue
 * 0 - student
 * 1 - batch
 * 2 - interrupts?
 * 3 - system
 */

struct multilevel_queue {
	queue_t** queues[]; //NOTE: we want an array of queues, I think this is the way to do it, though we may only need it to be a pointer?
	int num_levels;
};

/*
 * Returns an empty multilevel queue with number_of_levels levels.
 * Returns NULL on error.
 */
multilevel_queue_t* multilevel_queue_new(int number_of_levels)
{
	multilevel_queue_t* ret = (multilevel_queue_t*)malloc(sizeof(multilevel_queue_t));
	if (ret == NULL)
	{
		//malloc failed!
		return NULL;
	}
	//malloc didn't fail! Do things
	ret->num_levels = number_of_levels;
	queue_t** queues[number_of_levels];
	ret->queues = queues;
	int x;
	for (x = 0; x < number_of_levels; x++) {
		ret->queues[x] = queue_new();
		if (ret->queues[x] == NULL)
		{
			//failed to create one of the levels
			return NULL;
		}
	}
	return ret;
}

/*
 * Appends a void* to the multilevel queue at the specified level.
 * Return 0 (success) or -1 (failure).
 */
int multilevel_queue_enqueue(multilevel_queue_t* queue, int level, void* item)
{
	//append returns 0 or -1, as enqueue is supposed to 
	return queue_append(queue->queues[level], item);
}

/*
 * Dequeue and return the first void* from the multilevel queue starting at the specified level. 
 * Levels wrap around so as long as there is something in the multilevel queue an item should be returned.
 * Return the level that the item was located on and that item.
 * If the multilevel queue is empty, return -1 (failure) with a NULL item.
 */
int multilevel_queue_dequeue(multilevel_queue_t* queue, int level, void** item)
{
	return queue_dequeue(queue->queues[level], item);
}

/* 
 * Free the queue and return 0 (success) or -1 (failure).
 * Do not free the queue nodes; this is the responsibility of the programmer.
 */
int multilevel_queue_free(multilevel_queue_t* queue)
{
	//
	if (queue == NULL)
	{
		return -1;
	}
	//the below coode frees the queues... which we don't need to do. :P
	/*int x;
	for (x = 0; i < queue->num_levels; i++)
	{
		queue_free(queue->queues[x]);
	}*/
	free(queue);
	return 0;
}
