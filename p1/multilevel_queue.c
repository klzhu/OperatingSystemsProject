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
	int num_levels;
	int items;
	queue_t** queues; //NOTE: we want an array of queues, I think this is the way to do it, though we may only need it to be a pointer?
};

/*
 * Returns an empty multilevel queue with number_of_levels levels.
 * Returns NULL on error.
 */
multilevel_queue_t* multilevel_queue_new(int number_of_levels)
{
	if (number_of_levels <= 0) return NULL; //number of levels should be at least 1

	multilevel_queue_t* ret = (multilevel_queue_t*)malloc(sizeof(multilevel_queue_t));
	if (ret == NULL) return NULL; // malloc fialed

	//malloc didn't fail! Do things
	ret->num_levels = number_of_levels;
	ret->items = 0;
	ret->queues = (queue_t**)malloc(sizeof(queue_t*)*number_of_levels);
	
	if (ret->queues == NULL) //if malloc failed
	{
		free(ret); //free just allocated mem
		return NULL;
	}

	//create queues
	int x;
	for (x = 0; x < number_of_levels; x++) {
		ret->queues[x] = queue_new();
		if (ret->queues[x] == NULL)
		{
			//failed to create one of the levels
			// free successfully created queues
			int y;
			for (y = 0; y < x; y++)	queue_free(ret->queues[y]);

			free(ret);

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
	//validate inputs
	if (queue == NULL || level < 0 || level >= queue->num_levels) return -1;

	int good;
	//append returns 0 or -1, as enqueue is supposed to 
	good = queue_append(queue->queues[level], item);
	if (good == 0)
	{
		//success!
		queue->items++;
	}
	//will be -1 if dequeueing failed, else 0
	return good;
}

/*
 * Dequeue and return the first void* from the multilevel queue starting at the specified level. 
 * Levels wrap around so as long as there is something in the multilevel queue an item should be returned.
 * Return the level that the item was located on and that item.
 * If the multilevel queue is empty, return -1 (failure) with a NULL item.
 */
int multilevel_queue_dequeue(multilevel_queue_t* queue, int level, void** item)
{
	//validate inputs
	if (queue == NULL || level < 0 || level >= queue->num_levels) return -1;

	if (queue->items == 0)
	{
		//handle empty multiqueue 
		*item = NULL;
		return -1;
	}
	//dequeue element logic
	int good;
	//check target queue
	if (queue_length(queue->queues[level]) != 0)
	{
		good = queue_dequeue(queue->queues[level], item);
	}
	else
	{
		//check next queue
		if (queue_length(queue->queues[((level + 1) % 4)]) != 0)
		{
			good = queue_dequeue(queue->queues[((level + 1) % 4)], item);
		}
		else
		{
			//check next queue
			if (queue_length(queue->queues[((level + 2) % 4)]) != 0)
			{
				good = queue_dequeue(queue->queues[((level + 2) % 4)], item);
			}
			else
			{
				//check final queue - must have an element if reaching this step
				if (queue_length(queue->queues[((level + 3) % 4)]) != 0)
				{
					good = queue_dequeue(queue->queues[((level + 3) % 4)], item);
				}
				else
				{
					good = -1;
					printf("good set to -1: something is wrong and dequeue fell through all ");
				}
			}
		}
	}
	//return logic
	if (good == 0)
	{
		//success!
		queue->items--;
	}
	//will be -1 if dequeueing failed, else 0
	return good;
}

/* 
 * Free the queue and return 0 (success) or -1 (failure).
 * Do not free the queue nodes; this is the responsibility of the programmer.
 */
int multilevel_queue_free(multilevel_queue_t* queue)
{
	//validate input
	if (queue == NULL) return -1;

	int retVal = 0;
	int k;

	for (k = 0; k < queue->num_levels; k++) //iterate through and try to free every queue
	{
		int opVal = queue_free(queue->queues[k]);
		if (opVal == -1) retVal = opVal; // if there is error, retVal should be set to -1
	}

	free(queue->queues); //free queues
	free(queue); //free multiqueue
	return retVal;
}
