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
queue_t* g_waitingQueue;//global queue for waiting threads (for interrupts, etc)
queue_t* g_runnableQueue; //global queue for threads waiting to run, head of queue is currently running thread
int g_threadIdCounter = 0; //counter for creating unique threadIds

 typedef struct minithread{
 	int threadId;
 	stack_pointer_t* stackbase;
 	stack_pointer_t* stacktop;
 	bool runnable;
 }minithread;



/* minithread functions */

minithread_t*
minithread_fork(proc_t proc, arg_t arg) {
	if (proc == NULL || arg == NULL) return NULL;

	minithread_t* mt = minithread_create(proc,arg);
	mt->runnable = true;
	queue_append(g_runnableQueue, mt);
    return mt;
}

minithread_t*
minithread_create(proc_t proc, arg_t arg) {
	if (proc == NULL || arg == NULL) return NULL;

	minithread_t* mt = malloc(sizeof(minithread_t));
	if (mt == NULL) return NULL;

	proc_t cleanup = NULL;//cleanup code should wake up reaper thread to free stack and tcb. then context switch to next runnable thread?
	minithread_allocate_stack(mt->stackbase, mt->stacktop);
	minithread_initialize_stack(mt->stacktop,proc, arg, cleanup, arg);
	mt->runnable = false;
	mt->threadId=threadIdCounter++;
	queue_append(g_runnableQueue, mt);//add to runnable queue, but it's bool to run is set to false
	return mt;
}

minithread_t*
minithread_self() {
	//the current running thread is at the front of the runnable queue
	if (g_runnableQueue == NULL || queue_length(g_runnableQueue) == 0) return NULL;

	void** currRunningThread = NULL;
	int dequeueSuccess = queue_dequeue(g_runnableQueue, currRunningThread);
	
	if (dequeueSuccess == -1) return NULL;

	return *currRunningThread;
}

int
minithread_id() {
	//the current running thread is at the front of the threadqueue
	if (g_runnableQueue == NULL || queue_length(g_runnableQueue) == 0) return -1;

	void** dequeuedNode = NULL;
	int dequeueSuccess = queue_dequeue(g_runnableQueue, dequeuedNode);
	
	if (dequeueSuccess == -1) return -1;

	minithread* currRunningThread = *((minithread**)dequeuedNode);
	return currRunningThread->threadId;
}

void
minithread_stop() {
}

void
minithread_start(minithread_t *t) {
	if (t == NULL) return NULL;
	
	t->runnable = true;
}

void
minithread_yield() {
	//use minithread_switch -> i think the context switching saves the registers? Not sure on that yet how the registers are saved/restored yet.--yeah, the method saves registers	
	//move currently executing thread to end of queue
	//save the current running thread struct
	/*Forces the caller to relinquish the processor and be put to the end of
 *  the ready queue.  Allows another thread to run.*///---ready queue= waiting or running??...probably waiting...
	if (g_runnableQueue == NULL || queue_length(g_runnableQueue)== 0) return NULL;

	void** yieldingThread = NULL;// = threadQueue->head; //store the currently executing thread
	int dequeueSuccess = queue_dequeue(g_runnableQueue, yieldingThread);

	if (dequeueSuccess == -1) return NULL;

	queue_append(g_runnableQueue, yieldingThread);
}

void
minithread_system_initialize(proc_t mainproc, arg_t mainarg) {
	/*Starts up the system, and initializes global datastructures
Creates a thread to run mainproc(mainarg)
This should be where all queues, global semaphores, etc.
are initialized.*/
g_runnableQueue=queue_new();
g_threadIdCounter = 0; //not sure if this needs to be initialized here since it's initialized above..
minithread* mainThread=minithread_fork(mainproc,mainarg);
}


