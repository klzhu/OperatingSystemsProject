/*
 * Multilevel queue manipulation functions  
 */
#include "multilevel_queue.h"
#include <stdlib.h>
#include <stdio.h>

/* multilevel queue
 * 0 - student
 * 1 - batch
 * 2 - interrupts?
 * 3 - system
 */

struct multilevel_queue {
	queue_t** queues[];
	int num_levels;
};

/*
 * Returns an empty multilevel queue with number_of_levels levels.
 * Returns NULL on error.
 */
multilevel_queue_t* multilevel_queue_new(int number_of_levels)
{
	//needs wrapped in error catch
	multilevel_queue_t* ret = (multilevel_queue_t*)malloc(sizeof(multilevel_queue_t));
	ret->num_levels = number_of_levels;
	queue_t** queues[number_of_levels];
	ret->queues = queues;
	int x;
	for (x = 0; x < number_of_levels; x++) {
		ret->queues[x] = queue_new();
	}
	return ret;
}

/*
 * Appends a void* to the multilevel queue at the specified level.
 * Return 0 (success) or -1 (failure).
 */
int multilevel_queue_enqueue(multilevel_queue_t* queue, int level, void* item)
{

	return -1;
}

/*
 * Dequeue and return the first void* from the multilevel queue starting at the specified level. 
 * Levels wrap around so as long as there is something in the multilevel queue an item should be returned.
 * Return the level that the item was located on and that item.
 * If the multilevel queue is empty, return -1 (failure) with a NULL item.
 */
int multilevel_queue_dequeue(multilevel_queue_t* queue, int level, void** item)
{

	return -1;
}

/* 
 * Free the queue and return 0 (success) or -1 (failure).
 * Do not free the queue nodes; this is the responsibility of the programmer.
 */
int multilevel_queue_free(multilevel_queue_t* queue)
{

	return -1;
}
