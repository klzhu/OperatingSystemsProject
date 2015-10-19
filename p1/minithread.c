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
#include "common.h"
#include "minimsg.h"

/*
* A minithread should be defined either in this file or in a private
* header file.  Minithreads have a stack pointer with to make procedure
* calls, a stackbase which points to the bottom of the procedure
* call stack, the ability to be enqueueed and dequeued, and any other state
* that you feel they must have.
*/

// Forward declaration of functions defined elsewhere
void minimsg_network_handler(network_interrupt_arg_t* arg);

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
typedef enum { NONE, RUN_QUEUE, ZOMBIE_QUEUE } thread_queue_name; // name of thread queues

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

// This function does same as minithread_yield() and minithread_stop.
// It takes in the thread state and which queue the thread should be added to as input
void minithread_yield_helper(thread_state status, thread_queue_name whichQueue);

//This function returns true if the thread is either the idle or reaper thread, which should not be in a queue
bool is_idle_or_reaper(minithread_t* mt)
{
	return (mt == g_idleThread || mt == g_reaperThread);
}

/*****		alarm handler	*****/
// This function wakes up a thread and put it to runQueue. 
// arg is the thread to wake up
void alarm_handler_function(void* arg)
{
#if !defined(NDEBUG)	// if assert is active (i.e., debugging), print out the following debug info
	printf("Alarm goes off to wake up thread (ID = %d) at interrupt count = %lu\n", ((minithread_t*)arg)->threadId, g_interruptCount);
#endif
	minithread_start((minithread_t*)arg);
}

/* minithread functions */

//final proc that is called after the body proc for a thread is done running
int cleanup_proc(arg_t arg)
{
	assert(is_idle_or_reaper(minithread_self()) == false); //idle and reaper thread should never end

	while (1) {  //final_proc should not return
		minithread_yield_helper(DONE, ZOMBIE_QUEUE); //scheduler will set calling thread to done and put it on zombie queue and yield
	}

	return -1; //should never reach here (never return)
}

//function in reaper thread to clean up threads in zomb queue
int reaper_thread_method(arg_t arg)
{
	assert(g_zombieQueue != NULL); //zombie queue must be initialized

	while (1) //runs forever so it never runs it's final proc
	{
		while (queue_length(g_zombieQueue) > 0) {
			minithread_t* threadToClean = NULL;

			interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupts as we remove thread from global zombi queue
			int dequeueSuccess = queue_dequeue(g_zombieQueue, (void**)&threadToClean);
			AbortOnCondition(dequeueSuccess != 0, "Queue_dequeue error in reaper_thread_method()");
			set_interrupt_level(old_level); //restore interrupt level once we are done

			assert(dequeueSuccess == 0 && threadToClean != NULL && threadToClean->stackbase != NULL);
			minithread_free_stack(threadToClean->stackbase);
			free(threadToClean);
		}

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
		assert(multilevel_queue_length(g_runQueue) >= 0); // self check
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
		AbortOnCondition(appendSuccess != 0, "Queue_append failed in minithread_create_helper()");
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

// We assume that each yield() and put back to runQueue would be counted as having consumed 1 quanta in multi-level queue management
void
minithread_yield_helper(thread_state status, thread_queue_name whichQueue)
{
	assert(g_runningThread != NULL && g_runQueue != NULL && g_zombieQueue != NULL);

	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupts as we start manipulating global vars and yielding

	minithread_t* yieldingThread = minithread_self(); //get calling thread
	assert(yieldingThread != NULL && yieldingThread->status == RUNNING);

	assert(multilevel_queue_length(g_runQueue) >= 0); // self check

													  //point g_runningThread thread we'll context switch to
	if (queue_length(g_zombieQueue) > 0) g_runningThread = g_reaperThread; //if there are threads needing clean up, call reaper
	else if (multilevel_queue_length(g_runQueue) == 0) {
		if (g_runningThread == g_idleThread) { //if the running thread is already the idle thread, return
			set_interrupt_level(old_level); //restore old interrupt level
			return;
		}
		else g_runningThread = g_idleThread; //if no threads left to run, switch to idle thread
	}
	else { // get a thread from run queue
#if !defined(NDEBUG)	// if assert is active (i.e., debugging), print out the following debug info
		printf("# of items in g_runQueue is: %d & g_current_level = %d\n", multilevel_queue_length(g_runQueue), g_current_level);
#endif
		int level = multilevel_queue_dequeue(g_runQueue, g_current_level, (void**)&g_runningThread); //cast g_runningThread to a void pointer
		AbortOnCondition(level == -1, "Queue_dequeue error in minithread_yield_helper()");
		if (level > g_current_level) { // no thread in current level, move to the next level
			g_current_level++;
			if (g_current_level == NUMBER_OF_LEVELS_OF_ML_THREAD) g_current_level = 0;
			g_quantaCountdown = INITIAL_QUEUE_QUANTA[g_current_level];
		}
		else {
			g_quantaCountdown--;
			if (g_quantaCountdown == 0) {
				g_current_level++;
				if (g_current_level == NUMBER_OF_LEVELS_OF_ML_THREAD) g_current_level = 0;
				g_quantaCountdown = INITIAL_QUEUE_QUANTA[g_current_level];
			}
		}
		assert(g_runningThread != NULL && g_runningThread->status == READY);
	}

	//set the yielding thread status, insert it into a queue if necessary
	yieldingThread->status = status;
	if (whichQueue == ZOMBIE_QUEUE) // put the yielding thread to zombie queue
	{
		int appendSuccess = queue_append(g_zombieQueue, yieldingThread);
		AbortOnCondition(appendSuccess != 0, "Queue append error in minithread_yield_helper()");
	}
	else if (whichQueue == RUN_QUEUE) { // put the yielding thread to run queue
		yieldingThread->quanta--;	// It has used up 1 quanta
		if (yieldingThread->quanta == 0 && yieldingThread->level < NUMBER_OF_LEVELS_OF_ML_THREAD - 1) { // no level increase if already in highest level
			yieldingThread->level++;
			yieldingThread->quanta = INITIAL_THREAD_QUANTA[yieldingThread->level];	// initialize quanta to the level's intial quanta
		}
		int appendSuccess = multilevel_queue_enqueue(g_runQueue, yieldingThread->level, yieldingThread);
		AbortOnCondition(appendSuccess != 0, "Queue append error in minithread_yield_helper()");
	}

	//context switch to new running thread
	assert(g_runningThread != NULL);
	g_runningThread->status = RUNNING;
	minithread_switch(&(yieldingThread->stacktop), &(g_runningThread->stacktop)); //this will reenable interrupts automatically
}

void
minithread_stop()
{
	assert(is_idle_or_reaper(minithread_self()) == false); //idle and reaper threads should never have the WAIT status
	minithread_yield_helper(WAIT, NONE); //yield processor, set status to WAIT, and don't add thread to any queue
}

/*Forces the caller to relinquish the processor and be put to the end of
the ready queue.  Allows another thread to run. */
void
minithread_yield()
{
	//if idle or reaper thread, don't add to any queue, otherwise, add to run queue
	thread_queue_name whichQueue = is_idle_or_reaper(minithread_self()) ? NONE : RUN_QUEUE;
	minithread_yield_helper(READY, whichQueue);
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
#if !defined(NDEBUG)	// if assert is active (i.e., debugging), print out the following debug info
	printf("Enter clock_handler(), yield current thread (ID = %d) at interrupt count = %lu\n", minithread_id(), g_interruptCount);
#endif
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
	int netInitSuccess = network_initialize(minimsg_network_handler);
	minimsg_initialize(); //initialize our minimsg layer

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
