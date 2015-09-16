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

 //if condition is true, abort and give error message
#define AbortOnCondition(cond,message)                       \
    if (cond) {                                              \
        printf("Abort: %s:%d %d, MSG:%s\n",                  \
               __FILE__, __LINE__, message); \
        exit(1);                                             \
    }

//----- Global Variables ------
minithread_t* g_runningThread = NULL; //points to currently running thread
minithread_t* g_idleThread = NULL; //our idle thread that runs if no threads are left to run
minithread_t* g_reaperThread = NULL; //thread to clean up threads in the zombie queue

queue_t* g_runQueue = NULL; //global queue for threads waiting to run
queue_t* g_waitQueue = NULL; //global queue for threads not scheduled to run
queue_t* g_zombieQueue = NULL; //global queue for finished threads waiting to be cleaned up

int g_threadIdCounter = 0; //counter for creating unique threadIds

semaphore_t* g_mutexLock = NULL; //global lock

//Thread statuses
typedef enum { RUNNING, READY, WAIT, DONE } thread_state; // ready indicates scheduled to run.

 typedef struct minithread{
 	int threadId;
 	stack_pointer_t stackbase;
 	stack_pointer_t stacktop;
	thread_state status;
 } minithread;


 //   -----   Private helper functions  -----  
 // This function performs minithread_fork() or minithread_create() depending on int whichQueue
 // and also creates special threads that won't be added to any queue
 //cases:  whichQueue == 0:	the thread's status is set to READY and is not inserted into any queue 
 //				 1: the thread's status is set to READY and is inserted into run queue
 //			  else:	the thread's status is set to WAIT and is inserted into wait queue
 minithread_t* minithread_create_helper(proc_t proc, arg_t arg, int whichQueue);

 // This function does same as minithread_yield() except the thread is inserted into a queue as follows
 // setToRunnable == true: Set the outgoing thread's status to READY and, if the thread is not a globally pointed thread, is inserted to ready queue.
 //				   false: Set the outgoing thread's status to WAIT and is inserted to wait queue.
 void minithread_scheduler(bool setToRunnable);

/* minithread functions */

//final proc that is called after the body proc for a thread is done running
 int cleanup_proc(arg_t arg){
	 assert(g_zombieQueue != NULL); //zombie queue must be initialized already

	 minithread_t* mt = minithread_self(); //get calling thread, set status to done
	 mt->status = DONE;

	 //critical section, puts finished thread onto zombie queue for clean up
	 semaphore_P(g_mutexLock);
	 queue_append(g_zombieQueue,mt);
	 semaphore_V(g_mutexLock);

	 while(1){ 	 //final_proc should not return
        minithread_yield();
	 }

	 return -1; //should never return
}

 //function in reaper thread to clean up threads in zomb queue
 int reaper_thread_method(arg_t arg) {
	 assert(g_zombieQueue != NULL); //zombie queue must be initialized

	 while (1) //runs forever so it never runs it's final proc
	 {
		 while (queue_length(g_zombieQueue) > 0)
		 {
			 minithread_t* threadToClean = NULL;
			 int dequeueSuccess = queue_dequeue(g_zombieQueue, (void**)&threadToClean);

			 assert(dequeueSuccess == 0 && threadToClean != NULL && threadToClean->stackbase != NULL);
			 minithread_free_stack(threadToClean->stackbase);
			 free(threadToClean);
		 }

		 minithread_yield();
	 }

	 return -1; //should never return
 }

 //function in idle thread, checks if runnable queue has anything to run
int idle_thread_method(arg_t arg){
	assert(g_runQueue != NULL); //run queue must be initialized

	while(1) //run forever
	{
		while (queue_length(g_runQueue) == 0); //if there are no threads in run queue to run, loop
		
		minithread_yield();
	}

	return -1; //should never return
}


// ---- minithread ----
minithread_t* minithread_create_helper(proc_t proc, arg_t arg, int whichQueue)
{
	if (proc == NULL) return NULL;

	minithread* mt = malloc(sizeof(minithread_t));
	if (mt == NULL) return NULL; //if malloc errored

	//allocate stack for thread
	minithread_allocate_stack(&(mt->stackbase), &(mt->stacktop));
	minithread_initialize_stack(&(mt->stacktop), proc, arg, cleanup_proc, NULL);

	queue_t* globalQueueName = NULL; //points to the queue that the thread should be inserted into
	switch (whichQueue) {
	case 0: //the thread's status is set to READY and is not inserted into any queue, so leave globalQueueName as NULL 
		mt->status = READY;
		break;
	case 1: //the thread's status is set to READY and is inserted into run queue
		mt->status = READY;
		globalQueueName = g_runQueue;
		break;
	DEFAULT: //thread status is set to WAIT and is inserted into wait queue
		mt->status = WAIT;
		globalQueueName = g_waitQueue;
		break;
	}

	//set global variables, enter critical section
	assert(g_mutexLock != NULL); //our lock must not be null
	semaphore_P(g_mutexLock); // acquire lock
	mt->threadId = g_threadIdCounter++;
	if (globalQueueName != NULL) //there is a global queue our thread should be added to
	{
		int appendSuccess = queue_append(globalQueueName, mt);
		AbortOnCondition(appendSuccess != 0, "Queue_append failed in minithread_create_helper()");
	}
	semaphore_V(g_mutexLock); //release lock

	return mt;
}

minithread_t*
minithread_fork(proc_t proc, arg_t arg) {
	return minithread_create_helper(proc, arg, 1);
}

minithread_t*
minithread_create(proc_t proc, arg_t arg) {
	return minithread_create_helper(proc, arg, NULL);
}

minithread_t*
minithread_self() {
	//if there is no runnin thread currently
	if (g_runningThread == NULL) return NULL;

	return g_runningThread;
}

int
minithread_id() {
	//the current running thread is pointed to by g_runningThread
	if (g_runningThread == NULL) return -1;

	return g_runningThread->threadId;
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

	queue_append(g_runQueue, t);
}

void
minithread_yield() {	
	/*Forces the caller to relinquish the processor and be put to the end of
    the ready queue.  Allows another thread to run. */
	if (g_runQueue == NULL) return;
	 
	if (queue_length(g_runQueue) == 0) //if no threads waiting to run, idle thread runs
	{
		minithread_switch(&(g_runningThread->stacktop), &(g_idleThread->stacktop));
		g_runningThread = g_idleThread;
	}
	else
	{
		minithread_t* dequeuedThread = NULL;
		int dequeueSuccess = queue_dequeue(g_runQueue, (void**) &dequeuedThread); //cast dequeuedThread to a void pointer
		if (dequeueSuccess == -1 || dequeuedThread == NULL || dequeuedThread->stacktop == NULL) return;


		queue_append(g_runQueue, g_runningThread); //puts yielding thread back onto queue
		minithread_switch(&(g_runningThread->stacktop), &(dequeuedThread->stacktop)); //context switch to the dequeued thread, which is the next thread scheduled to run
		g_runningThread = dequeuedThread; //point the global running thread pointer to the new running thread
	}
}

void
minithread_system_initialize(proc_t mainproc, arg_t mainarg) {
	/*Starts up the system, and initializes global datastructures
	Creates a thread to run mainproc(mainarg)
	This should be where all queues, global semaphores, etc.
	are initialized.*/

	//initialize global variables
	//g_nonRunnableQueue =queue_new();
	g_runQueue=queue_new();
	g_zombieQueue=queue_new();
	g_threadIdCounter = 0;
	g_mutexLock = semaphore_create();
	g_reaperThread = minithread_create(reaper_thread_method, NULL);
	g_idleThread = minithread_create(idle_thread_method, NULL);
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

	semaphore_initialize(g_lock, 1);

	minithread_switch(kernelThreadStackPtr, &(g_runningThread->stacktop));
}

