/*
 * minithread.c:
 *      This file provides a few function headers for the procedures that
 *      you are required to implement for the minithread assignment.
 *
 *      EXCEPT WHERE NOTED YOUR IMPLEMENTATION MUST CONFORM TO THE
 *      NAMING AND TYPING OF THESE PROCEDURES.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include "minithread.h"
#include "queue.h"
#include "synch.h"
#include "machineprimitives.h"
#include <assert.h>

/*
 * A minithread should be defined either in this file or in a private
 * header file.  Minithreads have a stack pointer with to make procedure
 * calls, a stackbase which points to the bottom of the procedure
 * call stack, the ability to be enqueueed and dequeued, and any other state
 * that you feel they must have.
 */

typedef enum {NEW, RUNNABLE, RUNNING, WAITING, DONE} threadState;
int idCounter=0;

 typedef struct minithread{
 	int threadId;
 	void* stackptr;
 	stack_pointer_t* stackbase;
 	stack_pointer_t* stacktop;
 	void* programCtr;
 	queue_t* queue;
 	threadState tState;
 }minithread;


/* minithread functions */

minithread_t*
minithread_fork(proc_t proc, arg_t arg) {
		/*minithread* mt;
	minithread_allocate_stack(mt->stackbase, mt->stacktop);
	minithread_initialize_stack(mt->stacktop,proc, arg, cleanup, mt);
	mt->tState=RUNNING;//??
	mt->stackptr=stackbase;//??
	mt->programCtr=0;//??
	++idCounter;
	mt->threadId=idCounter;*/
	minithread* mt= minithread_create(proc,arg);
	minithread_start(mt);
    return mt;
}

void cleanup(minithread* md){
	while(true==true){}
}
minithread_t*
minithread_create(proc_t proc, arg_t arg) {
	minithread* mt;
	minithread_allocate_stack(mt->stackbase, mt->stacktop);
	minithread_initialize_stack(mt->stacktop,proc, arg, cleanup, mt);
	mt->tState=NEW;
	mt->stackptr=stackbase;//??
	mt->programCtr=0;//??
	++idCounter;
	mt->threadId=idCounter;
	return mt;
}

minithread_t*
minithread_self() {
	return NULL;
}

int
minithread_id() {
	/*if (minithread_t == NULL)
	{
		return -1;
	}
	return minithread_t->threadId;*/
	return -1;
}

void
minithread_stop() {
}

void
minithread_start(minithread_t *t) {
	if (t == NULL)
	{
		return;
	}
	else
	{
		t->tState = RUNNABLE;
		//put thread on running queue!
	}
}

void
minithread_yield() {
}

void
minithread_system_initialize(proc_t mainproc, arg_t mainarg) {
}


