#define NDEBUG 

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "defs.h"
#include "synch.h"
#include "queue.h"
#include "minithread.h"
#include "interrupts.h"

/*
 *      You must implement the procedures and types defined in this interface.
 */

/*
 * Semaphores.
 */
struct semaphore {
	int count;
	queue_t* semaWaitQ; //sema waiting queue
};


semaphore_t* semaphore_create() {
	semaphore_t *s = malloc(sizeof(semaphore_t));
	if (s == NULL) return NULL;

	s->count = -1; //set to invalid value to ensure semaphore_initialize() called before using semaphore
	s->semaWaitQ = queue_new();

	if (s->semaWaitQ == NULL)
	{
		free(s); //free memory just allocated
		return NULL;
	}

	return s;
}

void semaphore_destroy(semaphore_t *sem) {
	//Validate input arguments, abort if invalid argument is seen
	AbortOnCondition(sem == NULL, "Null argument sem in semaphore_destroy()");
	assert(sem->semaWaitQ != NULL); //sanity check

	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interruption

	//critical section
	int freeQueueSuccess = queue_free(sem->semaWaitQ); //release waiting queue
	AbortOnCondition(freeQueueSuccess != 0, "Free Queue failed in semaphore_destroy()");
	free(sem); //release semaphore

	set_interrupt_level(old_level); //restore interruption level
}

void semaphore_initialize(semaphore_t *sem, int cnt) {
	//Validate input arguments, abort if invalid argument is seen
	AbortOnCondition(sem == NULL || cnt < 0, "Invalid arguments passed to semaphore_initialize()");

	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupts

	//critical section
	sem->count = cnt;
	assert(sem->semaWaitQ != NULL); //sanity checks
	assert(sem->count == cnt);

	set_interrupt_level(old_level); //restore interrupts
}

void semaphore_P(semaphore_t *sem) {
	//Validate input arguments, abort if invalid argument is seen
	AbortOnCondition(sem == NULL, "Null argument sem in semaphore_P()");

	assert(sem->semaWaitQ != NULL); //sanity check

	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupts

	//critical section
	if (sem->count > 0) sem->count--;
	else
	{
		minithread_t* currThread = minithread_self(); //get the calling thread
		AbortOnCondition(currThread == NULL, "Failed in minithread_self() method in semaphore_P()");
		queue_append(sem->semaWaitQ, currThread); //put thread onto semaphore's wait queue

		minithread_stop(); //block calling thread, yield processor
	}
	set_interrupt_level(old_level); //restore interrupt level
}

void semaphore_V(semaphore_t *sem) {
	//Validate input arguments, abort if invalid argument is seen
	AbortOnCondition(sem == NULL, "Null argument sem in semaphore_V()"); //validate argument

	assert(sem->semaWaitQ != NULL);

	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupts

	//critical section
	if (queue_length(sem->semaWaitQ) == 0) sem->count++;
	else
	{
		//if the semaphore wait queue is not empty, then there are threads waiting and the count must be at 0
		assert(sem->count == 0);

		minithread_t* t = NULL;
		int dequeueSuccess = queue_dequeue(sem->semaWaitQ, (void**) &t);
		assert(t != NULL);
		AbortOnCondition(dequeueSuccess != 0, "Failed in queue_dequeue operation in semaphore_V()");
		
		minithread_start(t);
	}
	set_interrupt_level(old_level); //restore interrupts
}
