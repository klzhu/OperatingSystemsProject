#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "defs.h"
#include "synch.h"
#include "queue.h"
#include "minithread.h"

/*
 *      You must implement the procedures and types defined in this interface.
 */

 //If condition, fail gracefully and give error message
#define AbortGracefully(cond,message)                      	\
    if (cond) {                                             \
        printf("Abort: %s:%d, MSG:%s\n",                  	\
               __FILE__, __LINE__, message); 				\
        exit(1);                                             \
    }

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
	if (sem == NULL) return;

	assert(sem->semaWaitQ != NULL); //sanity check
	int freeQueueSuccess = queue_free(sem->semaWaitQ); //release waiting queue
	AbortGracefully(freeQueueSuccess != 0, "Free Queue failed in semaphore_destroy()");
	free(sem); //release semaphore
}

void semaphore_initialize(semaphore_t *sem, int cnt) {
	//Validate input arguments, abort if invalid argument is seen
	AbortGracefully(sem == NULL, "Null argument sem in semaphore_initialize()");
	AbortGracefully(cnt < 0, "Invalid argument cnt seen in semaphore_initialize()");

	//critical section
	sem->count = cnt;
	assert(sem->semaWaitQ != NULL); //sanity checks
	assert(sem->count == cnt);
}

void semaphore_P(semaphore_t *sem) {
	AbortGracefully(sem == NULL, "Null argument sem in semaphore_P()"); //validate argument
	AbortGracefully(sem->count < 0, "Semaphore has not been initialized"); //ensure semaphore count has been initialized 

	assert(sem->semaWaitQ != NULL); //sanity check

	if (sem->count > 0)
	{
		sem->count--;
	}
	else
	{
		minithread_t* currThread = minithread_self(); //get the calling thread
		AbortGracefully(currThread == NULL, "Failed in minithread_self() method in semaphore_P()");
		queue_append(sem->semaWaitQ, currThread); //put thread onto semaphore's wait queue

		minithread_stop(); //block calling thread, yield processor
	}
}

void semaphore_V(semaphore_t *sem) {
	AbortGracefully(sem == NULL, "Null argument sem in semaphore_P()"); //validate argument
	AbortGracefully(sem->count < 0, "Semaphore has not been initialized"); //ensure semaphore count has been initialized 

	assert(sem->semaWaitQ != NULL);

	if (queue_length(sem->semaWaitQ) == 0) sem->count++;
	else
	{
		//if the semaphore wait queue is not empty, then there are threads waiting and the count must be at 0
		assert(sem->count == 0);

		minithread_t* t = NULL;
		int dequeueSuccess = queue_dequeue(sem->semaWaitQ, (void**) &t);
		assert(t != NULL);
		AbortGracefully(dequeueSuccess != 0, "Failed in queue_dequeue operation in semaphore_V()");
		
		minithread_start(t);
	}
}
