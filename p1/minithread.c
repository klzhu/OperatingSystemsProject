/*
 * minithread.c:
 *      This file provides a few function headers for the procedures that
 *      you are required to implement for the minithread assignment.
 *
 *      EXCEPT WHERE NOTED YOUR IMPLEMENTATION MUST CONFORM TO THE
 *      NAMING AND TYPING OF THESE PROCEDURES.
 *
 */

//#define NDEBUG REENABLE BEFORE SUBMITTING
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "minithread.h"
#include "queue.h"
#include "synch.h"
#include "machineprimitives.h"
#include "interrupts.h"
#include "alarm.h"


 /* Macro to fail gracefully. If condition, fail and give error message */
#define AbortGracefully(cond,message)                      	\
    if (cond) {                                             \
        printf("Abort: %s:%d, MSG:%s\n",                  	\
               __FILE__, __LINE__, message); 				\
        exit(1);                                             \
    }

/*
 * A minithread should be defined either in this file or in a private
 * header file.  Minithreads have a stack pointer with to make procedure
 * calls, a stackbase which points to the bottom of the procedure
 * call stack, the ability to be enqueueed and dequeued, and any other state
 * that you feel they must have.
 */

 //Clock interrupt period in millseconds
const int INTERRUPT_PERIOD_IN_MILLISECONDS = 100; // set to 100ms

// ----- Global Variables ------ //
minithread_t* g_runningThread = NULL; //points to currently running thread
minithread_t* g_idleThread = NULL; //our idle thread that runs if no threads are left to run
minithread_t* g_reaperThread = NULL; //thread to clean up threads in the zombie queue

queue_t* g_runQueue = NULL; //global queue for threads waiting to run
queue_t* g_zombieQueue = NULL; //global queue for finished threads waiting to be cleaned up

int g_threadIdCounter = 0; //counter for creating unique threadIds

uint64_t g_interruptCount = 0; //global counter to count how many interrupts has passed. This value should not overflow for years.

//Thread statuses
typedef enum { RUNNING, READY, WAIT, DONE } thread_state; // ready indicates scheduled to run.

 typedef struct minithread {
 	int threadId;
 	stack_pointer_t stackbase;
 	stack_pointer_t stacktop;
	thread_state status;
 } minithread;


 //   -----   Private helper functions  -----  
 // This function performs minithread_fork() or minithread_create().
 // It takes in the thread state and the queue the thread should be added to as input
 minithread_t* minithread_create_helper(proc_t proc, arg_t arg, thread_state status, queue_t* whichQueue);

 //This function returns true if the thread is either the idle or reaper thread, which should not be in a queue
 bool is_idle_or_reaper(minithread_t* mt) {
	 return (mt == g_idleThread || mt == g_reaperThread);
 }

 // This function does same as minithread_yield() and minithread_stop.
// It takes in the thread state and the queue the thread should be added to as input
 void minithread_yield_helper(thread_state status, queue_t* whichQueue);

/* minithread functions */

//final proc that is called after the body proc for a thread is done running
 int cleanup_proc(arg_t arg){
	 assert(is_idle_or_reaper(minithread_self()) == false); //idle and reaper thread should never end

	 while(1){ 	 //final_proc should not return
        minithread_yield_helper(DONE, g_zombieQueue); //scheduler will set calling thread to done and put it on zombie queue and yield
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

			 interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupts as we remove thread from global zomb queue
			 int dequeueSuccess = queue_dequeue(g_zombieQueue, (void**)&threadToClean);
			 AbortGracefully(dequeueSuccess != 0, "Queue_dequeue error in reaper_thread_method()");
			 set_interrupt_level(old_level); //restore interrupt level once we are done

			 assert(dequeueSuccess == 0 && threadToClean != NULL && threadToClean->stackbase != NULL);
			 minithread_free_stack(threadToClean->stackbase);
			 free(threadToClean);
		 }

		 minithread_yield(); //yield process to another thread
	 }

	 return -1; //should never return
 }

 //function in idle thread, checks if runnable queue has anything to run
int idle_thread_method(arg_t arg) {
	assert(g_runQueue != NULL); //run queue must be initialized

	while(1) //run forever
	{
		while (queue_length(g_runQueue) == 0); //if there are no threads in run queue to run, loop
		
		minithread_yield(); // yield process to another thread
	}

	return -1; //should never return
}

/*****		alarm handler	*****/
// This function wakes up a thread and put it to runQueue. 
// arg is the thread to wake up
void alarm_handler_function(void* arg)
{
	minithread_start((minithread_t*)arg);
}

// ---- minithread ----
minithread_t*
minithread_create_helper(proc_t proc, arg_t arg, thread_state status, queue_t* whichQueue) {
	if (proc == NULL) return NULL;

	minithread* mt = malloc(sizeof(minithread_t));
	if (mt == NULL) return NULL; //if malloc errored

	//allocate stack for thread
	minithread_allocate_stack(&(mt->stackbase), &(mt->stacktop));
	minithread_initialize_stack(&(mt->stacktop), proc, arg, cleanup_proc, NULL);

	mt->status = status; //set the thread's status according to the function input

	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupt as we enter crit section
	mt->threadId = g_threadIdCounter++;
	if (whichQueue != NULL) //if thread needs to be added to queue, add it
	{
		int appendSuccess = queue_append(whichQueue, mt);
		AbortGracefully(appendSuccess != 0, "Queue_append failed in minithread_create_helper()");
	}
	set_interrupt_level(old_level); //restore interrupt level as we leave crit section
	return mt;
}

minithread_t*
minithread_fork(proc_t proc, arg_t arg) {
	return minithread_create_helper(proc, arg, READY, g_runQueue); //set status to READY, add to run queue
}

minithread_t*
minithread_create(proc_t proc, arg_t arg) {
	return minithread_create_helper(proc, arg, WAIT, NULL); //set status to WAIT, not added to any queue, waiting threads handled by application
}

minithread_t*
minithread_self() {
	//if there is no running thread currently
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
minithread_start(minithread_t *t) {
	AbortGracefully(t == NULL, "Null argument in minithread_start()");

	//if thread is already running, in runqueue, or finished running return
	if (t->status == RUNNING || t->status == READY || t->status == DONE) return;

	t->status = READY;

	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupt as we modify global run queue
	int appendSuccess = queue_append(g_runQueue, t);
	AbortGracefully(appendSuccess != 0, "Queue_append error in minithread_start()");
	set_interrupt_level(old_level); //restore interrupt level
}

void
minithread_yield_helper(thread_state status, queue_t* whichQueue) {
	assert(g_runningThread != NULL && g_runQueue != NULL && g_zombieQueue != NULL);

	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupts as we start manipulating global vars

	minithread_t* yieldingThread = minithread_self(); //get calling thread
	assert(yieldingThread != NULL && yieldingThread->status == RUNNING);

	//point g_runningThread thread we'll context switch to
	if (queue_length(g_zombieQueue) > 0) g_runningThread = g_reaperThread; //if there are threads needing clean up, call reaper
	else if (queue_length(g_runQueue) == 0) 
	{
		if (g_runningThread == g_idleThread) { //if the running thread is already the idle thread, return
			set_interrupt_level(old_level); //restore old interrupt level
			return; 
		}
		else g_runningThread = g_idleThread; //if no threads left to run, switch to idle thread
	}
	else 
	{
		int dequeueSuccess = queue_dequeue(g_runQueue, (void**)&g_runningThread); //cast g_runningThread to a void pointer
		AbortGracefully(dequeueSuccess != 0, "Queue_dequeue error in minithread_yield_helper()");
		assert(g_runningThread != NULL && g_runningThread->status == READY);
	}

	//set the yielding thread status, insert it into queue if necessary
	yieldingThread->status = status;
	if (whichQueue != NULL) //if thread needs to be added to a queue, append it
	{
		int appendSuccess = queue_append(whichQueue, yieldingThread);
		AbortGracefully(appendSuccess != 0, "Queue append error in minithread_yield_helper()");
	}
	
	//context switch to new running thread
	assert(g_runningThread != NULL);
	g_runningThread->status = RUNNING;
	minithread_switch(&(yieldingThread->stacktop), &(g_runningThread->stacktop)); //this will reenable interrupts automatically
}

void
minithread_stop() {
	//yield processor, set status to WAIT, and don't add thread to any queue
	assert(is_idle_or_reaper(minithread_self()) == false); //idle and reaper threads should never have the WAIT status
	minithread_yield_helper(WAIT, NULL); 
}

/*Forces the caller to relinquish the processor and be put to the end of
the ready queue.  Allows another thread to run. */
void
minithread_yield() {	
	//if idle or reaper thread, don't add to any queue, otherwise, add to run queue
	queue_t* whichQueue = is_idle_or_reaper(minithread_self()) ? NULL : g_runQueue;
	minithread_yield_helper(READY, whichQueue);
}

/*
 * This is the clock interrupt handling routine.
 * You have to call minithread_clock_init with this
 * function as parameter in minithread_system_initialize
 */
void 
clock_handler(void* arg) {
	set_interrupt_level(DISABLED); //disable interrupts while we're in interrupt handler
	printf("Enter clock_handler(), yield current thread (ID = %d)\n", minithread_id()); //REMOVE BEFORE SUBMITTING
	
	g_interruptCount++; //increment interrupt count
	int alarmRunSuccess = alarm_check_and_run(); //check to see if any alarms need to go off and run them if needed
	AbortGracefully(alarmRunSuccess == -1, "Failed to run alarms in clock_handler()");

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
minithread_system_initialize(proc_t mainproc, arg_t mainarg) {
	/*Starts up the system, and initializes global datastructures
	Creates a thread to run mainproc(mainarg)
	This should be where all queues, global semaphores, etc.
	are initialized.*/

	//initialize global variables
	g_runQueue = queue_new(); AbortGracefully(g_runQueue == NULL, "Failed to initialize g_runQueue in minithread_system_initialize()");
	g_zombieQueue = queue_new(); AbortGracefully(g_zombieQueue == NULL, "Failed to initialize g_zombieQueue in minithread_system_initialize()");

	g_threadIdCounter = 0;
	g_interruptCount = 0;

	//the following threads will not be in any queue
	g_reaperThread = minithread_create_helper(reaper_thread_method, NULL, READY, NULL); AbortGracefully(g_reaperThread == NULL, "Failed to initialize g_reaperThread in minithread_system_initialize()");
	g_idleThread = minithread_create_helper(idle_thread_method, NULL, READY, NULL); AbortGracefully(g_idleThread == NULL, "Failed to initialize g_idleThread in minithread_system_initialize()");
	g_runningThread = minithread_create_helper(mainproc, mainarg, READY, NULL); AbortGracefully(g_runningThread == NULL, "Failed to initialize g_runningThread in minithread_system_initialize()");

	stack_pointer_t* kernelThreadStackPtr = malloc(sizeof(stack_pointer_t*)); //stack pointer to our kernel thread
	g_runningThread->status = RUNNING;

	minithread_clock_init(INTERRUPT_PERIOD_IN_MILLISECONDS*MILLISECOND, clock_handler); //install interrupt service, enabled by context switch from kernel thread

	minithread_switch(kernelThreadStackPtr, &(g_runningThread->stacktop)); //context switch to our minithread from kernel thread
}

/*
 * sleep with timeout in milliseconds
 */
void 
minithread_sleep_with_timeout(int delay) {
	set_interrupt_level(DISABLED); //disable interrupt as we add a new alarm due to global alarms queue
	alarm_id newAlarm = register_alarm(delay, alarm_handler_function, minithread_self());
	AbortGracefully(newAlarm == NULL, "Failed to create new alarm in minithread_sleep_with_timeout()");
	minithread_stop(); //give up processor
}

