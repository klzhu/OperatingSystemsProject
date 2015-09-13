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


/*
 * Semaphores.
 */
struct semaphore {
	int lock;
	int count;
	queue_t* semaWaitQ;
};


semaphore_t* semaphore_create() {
	semaphore_t *s = malloc(sizeof(semaphore_t));
	if (s == NULL) return NULL;

	s->count = 0;
	s->lock = 0; //signifies lock is released
	s->semaWaitQ = queue_new();

	if (s->semaWaitQ == NULL) return NULL;

	return s;
}

void semaphore_destroy(semaphore_t *sem) {
	assert(sem != NULL);
	assert(sem->semaWaitQ != NULL);
	if (sem == NULL) return;

	int freeQueueSuccess = queue_free(sem->semaWaitQ);
	if (freeQueueSuccess == -1) return;

	free(sem);
}

void semaphore_initialize(semaphore_t *sem, int cnt) {
	assert(sem != NULL);
	assert(cnt != NULL);
	if (sem == NULL || cnt == NULL) return;

	sem->count = cnt;
}

void semaphore_P(semaphore_t *sem) {
	assert(sem != NULL);
	assert(sem->semaWaitQ != NULL);
	if (sem == NULL || sem->semaWaitQ == NULL) return;

	if (sem->count > 0) sem->count--;
	else
	{
		queue_append(sem->semaWaitQ, minithread_self);
		minithread_stop(); //should this be a thread yield or a thread stop?
	}
}

void semaphore_V(semaphore_t *sem) {
	assert(sem != NULL);
	assert(sem->semaWaitQ != NULL);
	if (sem == NULL || sem->semaWaitQ == NULL) return;

	if (queue_length(sem->semaWaitQ) == 0) sem->count++;
	else
	{
		//in class, the professor said we should assert that sem->count == 0 here, not sure why
		minithread_t** t = NULL;
		int dequeueSuccess = queue_dequeue(sem->semaWaitQ, t);

		assert(t != NULL);
		if (dequeueSuccess == -1) return;
		
		minithread_start(*t);
	}
}
