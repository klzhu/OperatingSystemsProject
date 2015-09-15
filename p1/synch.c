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
	assert(cnt >=0);
	if (sem == NULL || cnt <0) return;

	sem->count = cnt;
	assert(sem->count == cnt);
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
		//if the semaphore wait queue is not empty, then there are threads waiting and the count must be at 0
		assert(sem->count == 0);

		minithread_t** t = NULL;
		int dequeueSuccess = queue_dequeue(sem->semaWaitQ, (void**)t);

		assert(t != NULL);
		if (dequeueSuccess == -1) return;
		
		minithread_start(*t);
	}
}
