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
minithread* g_runningThread; //global variable that tracks the running thread
queue_t* g_nonRunnableQueue; //global queue for threads not scheduled to run
queue_t* g_runnableQueue; //global queue for threads waiting to run, head of queue is currently running thread
int g_threadIdCounter = 0; //counter for creating unique threadIds
semaphore_t* g_lock; //global lock

 typedef struct minithread{
 	int threadId;
 	stack_pointer_t* stackbase;
 	stack_pointer_t* stacktop;
 	proc_t* prc;//NOT SURE KEEP HERE OR CAN JUST CALL IN FORK METHOD?PIAZZA SAYS FORK ONLY SETS TO RUNNABLE
 	arg_t* ar;
 }minithread;



/* minithread functions */

minithread_t*
minithread_fork(proc_t proc, arg_t arg) {
	if (proc == NULL || arg == NULL) return NULL;

	minithread_t* mt = minithread_create(proc,arg);
	queue_append(g_runnableQueue, mt);
	//proc(arg);
    return mt;
}

int cleanup(arg_t arg){ //need to finish implementing cleanup method
	return -1;
}
minithread_t*
minithread_create(proc_t proc, arg_t arg) {
	if (proc == NULL || arg == NULL) return NULL;

	minithread_t* mt = malloc(sizeof(minithread_t));
	if (mt == NULL) return NULL;

	//proc_t cleanup = NULL;//cleanup code should wake up reaper thread to free stack and tcb. then context switch to next runnable thread?
	minithread_allocate_stack(mt->stackbase, mt->stacktop);
	minithread_initialize_stack(mt->stacktop,proc, arg, cleanup, arg);
	mt->threadId=g_threadIdCounter++;
	mt->prc=&proc;
	mt->ar=&arg;
	queue_append(g_nonRunnableQueue, mt);//add to non runnable queue, which holds threads not scheduled to run
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
	//the current running thread is pointed to by g_runningThread
	if (g_runningThread == NULL) return -1;

	return g_runningThread->threadId;
}

void
minithread_stop() {
	if (g_runningThread == NULL)
	{
		assert(false);
		return;
	}

	minithread_t** runningThreadPtr = g_runningThread;
	queue_append(g_nonRunnableQueue, g_runningThread);
	int dequeueSuccess = queue_dequeue(g_runnableQueue, runningThreadPtr);

	if (dequeueSuccess == -1)
	{
		assert(false);
		return;
	}
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
	//use minithread_switch -> i think the context switching saves the registers? Not sure on that yet how the registers are saved/restored yet.--yeah, the method saves registers	
	//move currently executing thread to end of queue
	//save the current running thread struct
	/*Forces the caller to relinquish the processor and be put to the end of
 *  the ready queue.  Allows another thread to run.*///---ready queue= waiting or running??...probably waiting...
	/* anothrer thread to run--which thread??...i guess for now dequeue  runnining thread and put it at end or queue and make the new queue head be running..
	so for now this method is fine*/
	if (g_runnableQueue == NULL || queue_length(g_runnableQueue)== 0) return;

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
	g_nonRunnableQueue =queue_new();
	g_runnableQueue=queue_new();
	g_threadIdCounter = 0;
	g_lock = semaphore_create();

	//need to check that our queues and lock were created correctly
	if (g_nonRunnableQueue == NULL || g_runnableQueue == NULL || g_lock == NULL)
	{
		//there is probably better code to fail gracefully and let the user know why the program failed, so this should be replaced eventually
		assert(false);
		return 0;
	}

	semaphore_initialize(g_lock, 1);

	minithread_t* mainThread;//=minithread_create(mainproc,mainarg);
	mainThread=minithread_fork(mainproc,mainarg);
	minithread_root();
	proc_t prc=*mainThread->prc;
	//arg_t ar=*mainThread->ar;
	//prc(ar); this segfaults :'(   how to get thread to execute the proc on its stack???
	mainproc(mainarg);
}


