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
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#include "minithread.h"
#include "synch.h"
#include "machineprimitives.h"
#include "interrupts.h"
#include "alarm.h"
#include "queue.h"
#include "multilevel_queue.h"
#include "network.h"
#include "minimsg.h"
#include "minisocket.h"

/*
* A minithread should be defined either in this file or in a private
* header file.  Minithreads have a stack pointer with to make procedure
* calls, a stackbase which points to the bottom of the procedure
* call stack, the ability to be enqueueed and dequeued, and any other state
* that you feel they must have.
*/

// Forward declaration of functions defined elsewhere
void common_network_handler(network_interrupt_arg_t* arg);

//Clock interrupt period in millseconds
const int INTERRUPT_PERIOD_IN_MILLISECONDS = 100; // set to 100ms
const int NUMBER_OF_LEVELS_OF_ML_THREAD = 4;	// Number of levels for multi-level threads
const int INITIAL_THREAD_QUANTA[] = { 1, 2, 4, 8 }; // Quanta (# of interrupts) set to each level, array size must match NUMBER_OF_LEVELS_OF_ML_THREAD
const int INITIAL_QUEUE_QUANTA[] = { 80, 40, 24, 16 }; // Quanta (# of interrupts) set to each level, array size must match NUMBER_OF_LEVELS_OF_ML_THREAD

// ----- Global Variables ------ //
minithread_t* g_runningThread = NULL; //points to currently running thread
minithread_t* g_idleThread = NULL; //our idle thread that runs if no threads are left to run
minithread_t* g_reaperThread = NULL; //thread to clean up threads in the zombie queue

multilevel_queue_t* g_runQueue = NULL; //global ml_queue for threads waiting to run
queue_t* g_zombieQueue = NULL; //global queue for finished threads waiting to be cleaned up

int g_threadIdCounter = 0; //counter for creating unique threadIds
int g_current_level = 0; //tracks current level of queue within multilevel queue
int g_quantaCountdown = -1; //global counter to keep track of how many quanta pass until runQueue switches its queue level for dequeue.

uint64_t g_interruptCount = 0; //global counter to count how many interrupts has passed. This value should not overflow for years.

//Thread statuses
typedef enum { RUNNING, READY, WAIT, DONE } thread_state; // thread's states.

struct minithread
{
	int threadId;				//unique minithread ID
	stack_pointer_t stackbase;	//pointer to base of thread's stack
	stack_pointer_t stacktop;	//pointer to top of thread's stack
	thread_state status;		//current thread status
	int level;					//current level within multilevel queue scheduler
	int quanta;					//current quanta left
};


//   -----   Private helper functions  -----  
// This function performs minithread_fork() or minithread_create().
// It takes in the thread state and which queue the thread should be added to as input
// Note that only valid input for whichQueue is NULL or g_runQueue.
minithread_t* minithread_create_helper(proc_t proc, arg_t arg, thread_state status, multilevel_queue_t* whichQueue);

// forward declaration (see the definition below for its functions) 
// This function does same as minithread_stop() except that caller can specify 
// thread's status to set, which queue to insert current thread, and which thread to yield to.
void minithread_stop_helper(thread_state status, queue_t* whichQueue, minithread_t* threadToRunNext);

//This function returns true if the thread is either the idle or reaper thread, which should not be in a queue
bool is_idle_or_reaper(minithread_t* mt)
{
	return (mt == g_idleThread || mt == g_reaperThread);
}

/*****	 alarm handler	 *****/
// This function wakes up a thread and put it to runQueue. 
// arg is the thread to wake up
void alarm_handler_function(void* arg)
{
	minithread_start((minithread_t*)arg);
}

/* minithread functions */

//final proc that is called after the body proc for a thread is done running
int cleanup_proc(arg_t arg)
{
	assert(is_idle_or_reaper(minithread_self()) == false); //idle and reaper thread should never end

	while (1) {  //final_proc should not return
		set_interrupt_level(DISABLED); //disable interrupts for yielding, interrupt is enabled by context switch
		// set the thread to DONE and put to zombie Queue, and yield to thread g_reaperThread to clean up DONE thread(s)
		minithread_stop_helper(DONE, g_zombieQueue, g_reaperThread);
	}

	return -1; //should never reach here (never return)
}

//function in reaper thread to clean up threads in zomb queue
int reaper_thread_method(arg_t arg)
{
	assert(g_zombieQueue != NULL); //zombie queue must be initialized

	while (1) //runs forever so it never runs it's final proc
	{
		interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupts as we access global zombie queue
		while (queue_length(g_zombieQueue) > 0) {
			minithread_t* threadToClean = NULL;
			int dequeueSuccess = queue_dequeue(g_zombieQueue, (void**)&threadToClean);
			assert(dequeueSuccess == 0 && threadToClean != NULL && threadToClean->stackbase != NULL);
			minithread_free_stack(threadToClean->stackbase);
			free(threadToClean);
		}

		set_interrupt_level(old_level); //restore interrupt level once we are done
		minithread_yield(); //yield process to another thread
	}

	return -1; //should never reach here (never return)
}

//function in idle thread, checks if runnable queue has anything to run
int idle_thread_method(arg_t arg)
{
	assert(g_runQueue != NULL); //run queue must be initialized

	while (1) //run forever
	{
		if (multilevel_queue_length(g_runQueue) > 0) { //if there is a thread in runQueue, yield to it
			minithread_yield(); // yield to another thread
		}
	}

	return -1; //should never reach here (never return)
}


// ---- minithread ----
minithread_t*
minithread_create_helper(proc_t proc, arg_t arg, thread_state status, multilevel_queue_t* whichQueue)
{
	if (proc == NULL) return NULL;

	minithread_t* mt = malloc(sizeof(minithread_t));
	if (mt == NULL) return NULL; //if malloc errored

	//allocate stack for thread
	minithread_allocate_stack(&(mt->stackbase), &(mt->stacktop));
	minithread_initialize_stack(&(mt->stacktop), proc, arg, cleanup_proc, NULL);

	mt->status = status;	//set the thread's status according to the function input
	mt->level = 0;			//set to default level 0
	mt->quanta = INITIAL_THREAD_QUANTA[mt->level]; // initialize its quanta

	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupt as we enter crit section
	mt->threadId = g_threadIdCounter++;
	if (whichQueue != NULL) //if thread needs to be added to queue, add it
	{
		int appendSuccess = multilevel_queue_enqueue(whichQueue, mt->level, mt);
		if (appendSuccess != 0) //error while enqueing our new thread
		{
			free(mt); //free newly created minithread
			mt = NULL;
		}
	}
	set_interrupt_level(old_level); //restore interrupt level as we leave crit section
	return mt;
}

minithread_t*
minithread_fork(proc_t proc, arg_t arg)
{
	return minithread_create_helper(proc, arg, READY, g_runQueue); //set status to READY, add to run queue
}

minithread_t*
minithread_create(proc_t proc, arg_t arg)
{
	return minithread_create_helper(proc, arg, WAIT, NULL); //set status to WAIT, not added to any queue, waiting threads handled by application
}

minithread_t*
minithread_self()
{
	assert(g_runningThread != NULL); // self checking
	return g_runningThread;
}

int
minithread_id()
{
	//the current running thread is pointed to by g_runningThread
	if (g_runningThread == NULL) return -1;

	return g_runningThread->threadId;
}

void
minithread_start(minithread_t *t)
{
	AbortOnCondition(t == NULL, "Null argument in minithread_start()");

	//if thread is already running, in runqueue, or finished running return
	if (t->status == RUNNING || t->status == READY || t->status == DONE) return;

	t->status = READY;

	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupt as we modify global run queue
	int appendSuccess = multilevel_queue_enqueue(g_runQueue, t->level, t);	// put to the same level queue
	AbortOnCondition(appendSuccess != 0, "Queue_append error in minithread_start()");
	set_interrupt_level(old_level); //restore interrupt level
}

// This function implements minithread_stop() with more flexibily.
// Inputs: 
//		status - The status that the current thread's status will be set to.
//		whichQueue -- which queue to put the current thread to
//		threadToRun -- the thread the current thread will yield to, which cannot be the calling function's thread.
// 
// NOTES:
//	1.	We assume that minithread_stop() does not consume any quanta.
//  2.  There is no protection for atomic operation inside this function. Caller should ensure
//		this function is executed in as an atomic operation by disabling interrupts
//  3.  It does not touch g_current_level or level's quanta. It is calling function's responsibility
void
minithread_stop_helper(thread_state status, queue_t* whichQueue, minithread_t* threadToRunNext)
{
	assert(threadToRunNext != NULL);

	minithread_t* yieldingThread = minithread_self(); //get calling thread
	assert(yieldingThread != NULL && yieldingThread->status == RUNNING && yieldingThread != threadToRunNext);

	yieldingThread->status = status; // set the yielding thread status
	if (whichQueue != NULL) // put the yielding thread to the queue
	{
		int appendSuccess = queue_append(whichQueue, yieldingThread);
		AbortOnCondition(appendSuccess != 0, "Queue append error in minithread_stop_helper()");
	}

	g_runningThread = threadToRunNext;
	g_runningThread->status = RUNNING;
	minithread_switch(&(yieldingThread->stacktop), &(g_runningThread->stacktop)); //this will reenable interrupts automatically
}

void
minithread_stop()
{
	assert(is_idle_or_reaper(minithread_self()) == false); //idle and reaper threads should never have the WAIT status

	set_interrupt_level(DISABLED); //disable interrupts for yielding, interrupt is enabled by context switch
	minithread_t* nextThread = g_idleThread;
	if (multilevel_queue_length(g_runQueue) > 0) { // If runQueue is not empty, get the next thread to see which one should run next, otherwise, nextThread is idle thread
		int nextLevel = multilevel_queue_dequeue(g_runQueue, g_current_level, (void**)&nextThread); // get and dequeue the next thread from runQueue
		assert(nextLevel != -1); //should not have returned an error code

		// update g_current_level to the level of the next-to-run thread if needed
		if (nextLevel != g_current_level) { // move g_current_level to nextLevel if there is no thread to run at the current level
			g_current_level = nextLevel; // move to nextLevel that has the next thread to run
			g_quantaCountdown = INITIAL_QUEUE_QUANTA[g_current_level]; // init the level's quanta
		}
	}

	minithread_stop_helper(WAIT, NULL, nextThread); //yield processor, set status to WAIT, and don't add thread to any queue. Context switch will automatically reenable interrupts
}

/*Forces the caller to relinquish the processor and be put to the end of
the ready queue.  Allows another thread to run. */
void
minithread_yield()
{
	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupts to ensure atomic operation

	minithread_t* currThread = minithread_self(); //get calling thread
	assert(currThread != NULL && currThread->status == RUNNING);

	minithread_t* nextThread = NULL; // NULL indicates keep running the current thread without context switch
	if (is_idle_or_reaper(currThread)) { // currThread is either the idle or reaper thread, we assume it does not count into the level's quanta
		if (multilevel_queue_length(g_runQueue) == 0) { // no thread in runQueue
			if (currThread == g_reaperThread) { // if the curr thread is the reaper thread, switch to g_idleThread
				nextThread = g_idleThread;
			}
		}
		else { // get next thread from runQueue
			int nextLevel = multilevel_queue_dequeue(g_runQueue, g_current_level, (void**)&nextThread); // get next thread to run
			assert(nextLevel >= 0 && nextThread != NULL);
		}
	}
	else {
		// if curr thread is not the idle or reaper thread, reduce its quanta by 1 and adjust its level if needed
		currThread->quanta--;	// It has used up 1 quanta
		if (currThread->quanta == 0 && currThread->level < NUMBER_OF_LEVELS_OF_ML_THREAD - 1) { // no level increase if already in highest level
			currThread->level++;
			currThread->quanta = INITIAL_THREAD_QUANTA[currThread->level];	// initialize quanta to the level's intial quanta
		}

		// Update the quanta count for the current level, and adjust the running level if needed
		g_quantaCountdown--;
		if (g_quantaCountdown == 0) {
			g_current_level++;
			if (g_current_level == NUMBER_OF_LEVELS_OF_ML_THREAD) g_current_level = 0;
			g_quantaCountdown = INITIAL_QUEUE_QUANTA[g_current_level];
		}

		if (multilevel_queue_length(g_runQueue) > 0) { // If runQueue is not empty, get the next thread to see which one should run next
			int nextLevel = multilevel_queue_peek(g_runQueue, g_current_level, (void**)&nextThread); // get the next thread from runQueue without dequeuing it
			assert(nextLevel >= 0 && nextThread != NULL && nextThread->level == nextLevel);

			// find whose level is closer to g_current_level, the closer should run next
			int lv = g_current_level;
			while (lv != currThread->level && lv != nextLevel) { // wrappingly increase level from g_current_level to see which one is hit first
				lv++;
				if (lv == NUMBER_OF_LEVELS_OF_ML_THREAD) lv = 0; // wrap around
			}

			if (lv == nextLevel) { // switch to next thread
				nextLevel = multilevel_queue_dequeue(g_runQueue, g_current_level, (void**)&nextThread); // dequeue the next thread from ruQueue
				assert(nextLevel >= 0 && nextThread != NULL && nextThread->level == nextLevel);
			}
			else { // currThread should continue to run, no need to context switch
				nextThread = NULL;
			}
		}
	}

	if (nextThread == NULL) { // no need to switch, keep running the current thread
		set_interrupt_level(old_level); //restore old interrupt level
		return;
	}
	else { // context switch to nextThread
		currThread->status = READY;
		int appendSuccess = multilevel_queue_enqueue(g_runQueue, currThread->level, currThread); // insert CurrThread to runQueue
		AbortOnCondition(appendSuccess == -1, "Failed to enqueue in minithread_yield()");

		assert(nextThread->status == READY);
		nextThread->status = RUNNING;
		g_runningThread = nextThread;
		minithread_switch(&(currThread->stacktop), &(g_runningThread->stacktop)); //this will reenable interrupts automatically
	}
}

/*
* This is the clock interrupt handling routine.
* You have to call minithread_clock_init with this
* function as parameter in minithread_system_initialize
*/
void
clock_handler(void* arg)
{
	set_interrupt_level(DISABLED); //disable interrupts while we're in interrupt handler

	g_interruptCount++; //increment interrupt count
	int alarmRunSuccess = alarm_check_and_run(); // set off alarms if any
	AbortOnCondition(alarmRunSuccess == -1, "Failed to run alarms in clock_handler()");

	minithread_yield(); //yield processor, context switch will automatically reenable interrupts
}

/*
* Initialization.
*
*      minithread_system_initialize:
*       This procedure should be called from your C main procedure
*       to turn a single threaded UNIX process into a multithreaded
*       program.
*
*       Initialize any private data structures.
*       Create the idle thread.
*       Fork the thread which should call mainproc(mainarg)
*       Start scheduling.
*
*/
void
minithread_system_initialize(proc_t mainproc, arg_t mainarg)
{
	/*Starts up the system, and initializes global datastructures
	Creates a thread to run mainproc(mainarg)
	This should be where all queues, global semaphores, etc.
	are initialized.*/

	//initialize global variables
	g_runQueue = multilevel_queue_new(NUMBER_OF_LEVELS_OF_ML_THREAD);
	g_zombieQueue = queue_new();

	g_threadIdCounter = 0;
	g_interruptCount = 0;
	g_current_level = 0;
	g_quantaCountdown = INITIAL_QUEUE_QUANTA[g_current_level];

	//the following threads will not be in any queue
	g_reaperThread = minithread_create_helper(reaper_thread_method, NULL, READY, NULL);
	g_idleThread = minithread_create_helper(idle_thread_method, NULL, READY, NULL);
	g_runningThread = minithread_create_helper(mainproc, mainarg, READY, NULL);

	// checking if any error occurs for above operations, and abort if error occurs
	AbortOnCondition(g_runQueue == NULL || g_zombieQueue == NULL || g_reaperThread == NULL || g_idleThread == NULL || g_runningThread == NULL, "Failed in minithread_system_initialize()");

	g_runningThread->status = RUNNING;

	minithread_clock_init(INTERRUPT_PERIOD_IN_MILLISECONDS*MILLISECOND, clock_handler); //install interrupt service, enabled by the context switch
	int netInitSuccess = network_initialize(common_network_handler);
	minimsg_initialize(); //initialize our minimsg layer
	minisocket_initialize(); //initialize our minisocket layer

	stack_pointer_t* kernelThreadStackPtr = malloc(sizeof(stack_pointer_t*)); //stack pointer to our kernel thread
	AbortOnCondition(netInitSuccess == -1 || kernelThreadStackPtr == NULL, "Failed in minithread_system_initialize()"); // check for errors and abort if error is found
	minithread_switch(kernelThreadStackPtr, &(g_runningThread->stacktop)); //context switch to our minithread from kernel thread, this enables interrupts by default
}

/*
* sleep with timeout in milliseconds
*/
void
minithread_sleep_with_timeout(int delay)
{
	alarm_id newAlarm = register_alarm(delay, alarm_handler_function, minithread_self());
	AbortOnCondition(newAlarm == NULL, "Failed to register an alarm in minithread_sleep_with_timeout()");
	minithread_stop(); //give up processor, this wil enable interrupt
}
