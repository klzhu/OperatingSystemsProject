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
#include "defs.h"
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
minithread_t* g_runningThread = NULL; //global variable that tracks the running thread
minithread_t* g_idleThread = NULL;
minithread_t* g_reaperThread = NULL;
//queue_t* g_nonRunnableQueue = NULL; //global queue for threads not scheduled to run
queue_t* g_runnableQueue = NULL; //global queue for threads waiting to run, head of queue is currently running thread
queue_t* g_zombieQueue=NULL;//global queue for zombie threads waiting to be cleaned up
int g_threadIdCounter = 0; //counter for creating unique threadIds
semaphore_t* g_lock = NULL; //global lock

 typedef struct minithread{
 	int threadId;
 	stack_pointer_t stackbase;
 	stack_pointer_t stacktop;
 }minithread;



/* minithread functions */

 int cleanup(arg_t arg){ //need to finish implementing cleanup method
	minithread_t* mt=g_runningThread;//(minithread_t*)arg;
	queue_append(g_zombieQueue,mt);
	//final_proc should not return
	while(1){
       minithread_yield();
	}
	return 0;

}

//int dummyarg = 1;

int idleThreadMethod(arg_t arg){
	while(1)
	{
		minithread_yield();
	}
	return -1;
}



minithread_t*
minithread_fork(proc_t proc, arg_t arg) {
	if (proc == NULL) return NULL;

	minithread_t* mt = malloc(sizeof(minithread_t));
	if (mt == NULL) return NULL;

	minithread_allocate_stack(mt->stackbase, mt->stacktop);
	minithread_initialize_stack(mt->stacktop, proc, arg, cleanup, arg);

	if (queue_length(g_runnableQueue) != 0) queue_append(g_runnableQueue, mt);
	//proc(arg);
    return mt;
}

minithread_t*
minithread_create(proc_t proc, arg_t arg) {
	if (proc == NULL) return NULL;

	minithread_t* mt = malloc(sizeof(minithread_t));
	if (mt == NULL) return NULL;

	//proc_t cleanup = NULL;//cleanup code should wake up reaper thread to free stack and tcb. then context switch to next runnable thread?
	minithread_allocate_stack(&(mt->stackbase), &(mt->stacktop));
	minithread_initialize_stack(&(mt->stacktop),proc, arg, cleanup, NULL);//&dummyarg);
	mt->threadId=g_threadIdCounter++;

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
	/*
	//the current running thread is pointed to by g_runningThread
	if (g_runningThread == NULL) return -1;

	return g_runningThread->threadId;
	*/
	return 1;
}

void
minithread_stop() {
	/*
	if (g_runningThread == NULL)
	{
		assert(false);
		return;
	}

	void** runningThreadPtr = g_runningThread;
	queue_append(g_nonRunnableQueue, g_runningThread);
	int dequeueSuccess = queue_dequeue(g_runnableQueue, runningThreadPtr);

	if (dequeueSuccess == -1)
	{
		assert(false);
		return;
	}

	//finish else
	*/
}

void
minithread_start(minithread_t *t) {
	//TO DO: Should use AbortOnCondition and AbortOnError to handle failing gracefully
	if (t == NULL) return;
	assert(t != NULL);

	queue_append(g_runnableQueue, t);
}

void
minithread_yield() {	
	/*Forces the caller to relinquish the processor and be put to the end of
 *  the ready queue.  Allows another thread to run.*///---ready queue= waiting or running??...probably waiting...
	/* anothrer thread to run--which thread??...i guess for now dequeue  runnining thread and put it at end or queue and make the new queue head be running..
	so for now this method is fine*/
	if (g_runnableQueue == NULL) return;
	 
	//if (queue_length(g_runnableQueue) == 0) minithread_switch();

	void** yieldingThread = NULL;// = threadQueue->head; //store the currently executing thread
	int dequeueSuccess = queue_dequeue(g_runnableQueue, yieldingThread);

	if (dequeueSuccess == -1) return;
	queue_append(g_runnableQueue, yieldingThread);

	void** nextThread = NULL;// to be new head
    dequeueSuccess = queue_dequeue(g_runnableQueue, nextThread);
    if(dequeueSuccess ==-1)return;//MAYBE BETTER WAY TO INDICATE FAILURE?
	minithread_switch(((minithread_t*)(*yieldingThread))->stackbase, ((minithread_t*)(*nextThread))->stackbase);//BASE OR TOP??? IS TYPE CASTING FROM VOID* OK??
	queue_prepend(g_runnableQueue, nextThread);
}

void
minithread_system_initialize(proc_t mainproc, arg_t mainarg) {
	/*Starts up the system, and initializes global datastructures
	Creates a thread to run mainproc(mainarg)
	This should be where all queues, global semaphores, etc.
	are initialized.*/

	//initialize global variables
	//g_nonRunnableQueue =queue_new();
	g_runnableQueue=queue_new();
	g_zombieQueue=queue_new();
	g_threadIdCounter = 0;
	g_lock = semaphore_create();
	g_reaperThread = minithread_create(cleanup, NULL);
	g_idleThread = minithread_create(idleThreadMethod, NULL);
	g_runningThread = minithread_create(mainproc, mainarg);

	stack_pointer_t* kernelThreadStackPtr = malloc(sizeof(stack_pointer_t*)); //stack pointer to our kernel thread

	//need to check that our queues and lock were created correctly
	if (g_runnableQueue == NULL || g_lock == NULL || g_reaperThread == NULL
		|| g_idleThread == NULL || g_runningThread == NULL || kernelThreadStackPtr == NULL)
	{
		//there is probably better code to fail gracefully and let the user know why the program failed, so this should be replaced eventually
		assert(false);
		return;
	}

	//semaphore_initialize(g_lock, 1);

	minithread_switch(kernelThreadStackPtr, &(g_runningThread->stacktop));
}


