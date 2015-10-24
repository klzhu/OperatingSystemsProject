/*
 * Multilevel queue manipulation functions  
 */
#ifndef __MULTILEVEL_QUEUE_H__
#define __MULTILEVEL_QUEUE_H__

#include "queue.h"

/*
 * multilevel_queue_t is a pointer to an internally maintained data structure.
 * Clients of this package do not need to know how the queues are
 * represented. They see and manipulate only multilevel_queue_t's. 
 */
typedef struct multilevel_queue multilevel_queue_t;

/*
 * Returns an empty multilevel queue with number_of_levels levels.
 * Returns NULL on error.
 */
multilevel_queue_t* multilevel_queue_new(int number_of_levels);

/*
 * Appends a void* to the multilevel queue at the specified level.
 * Return 0 (success) or -1 (failure).
 */
int multilevel_queue_enqueue(multilevel_queue_t* queue, int level, void* item);

/*
 * Dequeue and return the first void* from the multilevel queue starting at the specified level. 
 * Levels wrap around so as long as there is something in the multilevel queue an item should be returned.
 * Return the level that the item was located on and that item.
 * If the multilevel queue is empty, return -1 (failure) with a NULL item.
 */
int multilevel_queue_dequeue(multilevel_queue_t* queue, int level, void** item);

/* 
 * Free the queue and return 0 (success) or -1 (failure).
 * Do not free the queue nodes; this is the responsibility of the programmer.
 */
int multilevel_queue_free(multilevel_queue_t* queue);

/*
* Return the total number of items in the queue, or -1 if an error occured
*/
int multilevel_queue_length(const multilevel_queue_t* mlq);

/*
* This function is the same as multilevel_queue_dequeue() except the returned item is not dequeued, i.e.,
* return the first void* from the multilevel queue starting at the specified level without dequeuing it.
* Levels wrap around so as long as there is something in the multilevel queue an item should be returned.
* Return the level that the item was located on and that item.
* If the multilevel queue is empty, return -1 (failure) with a NULL item.
*/
int multilevel_queue_peek(multilevel_queue_t* queue, int level, void** item);

#endif /*__MULTILEVEL_QUEUE_H__*/
