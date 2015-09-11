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
#include <stdbool.h>
/*
 * A minithread should be defined either in this file or in a private
 * header file.  Minithreads have a stack pointer with to make procedure
 * calls, a stackbase which points to the bottom of the procedure
 * call stack, the ability to be enqueueed and dequeued, and any other state
 * that you feel they must have.
 */

//Global Variables
queue_t* waitingQueue;//is this the "ready" queue???
queue_t* runnableQueue; //or this????
int threadIdCounter; //counter for creating unique threadIds

 typedef struct minithread{
 	int threadId;
 	stack_pointer_t* stackbase;
 	stack_pointer_t* stacktop;
 	void* programCtr;
 	queue_t* queue;
 	bool runnable;
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
	minithread_t* mt = minithread_create(proc,arg);
	mt->runnable = true;
	queue_append(runnableQueue, mt);
    return mt;
}

minithread_t*
minithread_create(proc_t proc, arg_t arg) {
	minithread_t* mt;

proc_t cleanup;//cleanup code should wake up reaper thread to free stack and tcb. then context switch to next runnable thread?
	minithread_allocate_stack(mt->stackbase, mt->stacktop);
	minithread_initialize_stack(mt->stacktop,proc, arg, cleanup, arg);
	mt->runnable = false;
	mt->threadId=threadIdCounter++;
	queue_append(waitingQueue, mt);//??
	return mt;
}

minithread_t*
minithread_self() {
	//the current running thread is at the front of the threadqueue
	if (runnableQueue == NULL || queue_length(runnableQueue) == 0) 
		return NULL;
	void** tmpThread;
	int dequeueSuccess = queue_dequeue(runnableQueue,tmpThread);
	if (dequeueSuccess == -1) return NULL;
	else
	return *tmpThread;
}

int
minithread_id() {
	//the current running thread is at the front of the threadqueue
		if (runnableQueue == NULL || queue_length(runnableQueue) == 0)
	{
		return -1;
	}

	void** tmpThread;
	int dequeueSuccess = queue_dequeue(runnableQueue,tmpThread);
	if (dequeueSuccess == -1) return -1;
	else
	return (*(*tmpThread))->threadId;
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
		t->runnable = true;
	}
}

void
minithread_yield() {
	//use minithread_switch -> i think the context switching saves the registers? Not sure on that yet how the registers are saved/restored yet.--yeah, the method saves registers	
	//move currently executing thread to end of queue
	//save the current running thread struct
	/*Forces the caller to relinquish the processor and be put to the end of
 *  the ready queue.  Allows another thread to run.*///---ready queue= waiting or running??...probably waiting...
	if (runnableQueue == NULL || queue_length(runnableQueue)== 0) return;

	void** tmpThread;// = threadQueue->head; //store the currently executing thread
	int dequeueSuccess = queue_dequeue(runnableQueue,tmpThread);
	if (dequeueSuccess == -1) return;
	else
	{
		queue_append(runnableQueue, tmpThread);
	}
}

void
minithread_system_initialize(proc_t mainproc, arg_t mainarg) {
	/*Starts up the system, and initializes global datastructures
Creates a thread to run mainproc(mainarg)
This should be where all queues, global semaphores, etc.
are initialized.*/
runnableQueue=queue_new();
threadIdCounter=0;
minithread* mainThread=minithread_fork(mainproc,mainarg);
}


