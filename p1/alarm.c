#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include "interrupts.h"
#include "alarm.h"
#include "minithread.h"
#include "queue.h"
#include "synch.h"


/* Macro to fail gracefully. If condition, fail and give error message */
#define AbortGracefully(cond,message)                      	\
    if (cond) {                                             \
        printf("Abort: %s:%d, MSG:%s\n",                  	\
               __FILE__, __LINE__, message); 				\
        exit(1);                                             \
	}

// ---- Global variables ---- //
extern uint64_t g_interruptCount; //global counter to count how many interrupts has passed. This value should not overflow for years.
queue_t* g_alarmsQueue = NULL; //global queue that holds our alarms in sorted order according to when they should be set off

//struct for our alarm
typedef struct alarm {
	uint64_t interrupt_to_wake; //tracks at which global interrupt the alarm should go off
	minithread_t* sleepingThread; //handler to our sleeping thread for this alarm
	alarm_handler_t alarm_handler; //method to execute when alarm goes off
} alarm_t;

//alarm handler
int alarm_handler_function(arg_t arg) {
	assert(g_alarmsQueue != NULL); //global alarms queue should be initialized

	//call minithread_start on the thread that belongs to alarm

	return -1;
}

alarm_handler_t alarm_get_alarm_handler() {
	alarm_handler_t alarm_handler = alarm_handler_function;
	return alarm_handler;
}

int alarm_run() {
	//global alarms queue should be initialized, should only be ran by interrupt handler so interrupt count must be >0 
	assert(g_alarmsQueue != NULL && g_interruptCount > 0);

	alarm_t* currAlarm = NULL;
	int peekSuccess = queue_peek(g_alarmsQueue, (void**)&currAlarm); //peek at the head of our queue
	if (peekSuccess == -1) return -1; //peek alarm returned an error

	if (currAlarm == NULL) return 0; //there are no elements in our queue
	else if (currAlarm->interrupt_to_wake > g_interruptCount) return 0; //if the highest priority alarm is not scheduled to go off, none of them are and we can return
	else { //we have an alarm ready to go off
		while (currAlarm->interrupt_to_wake <= g_interruptCount) {
			alarm_t* alarmToRun = NULL;

			int dequeueSuccess = queue_dequeue(g_alarmsQueue, (void**)&alarmToRun); //remove the alarm from the queue, set it off
			if (dequeueSuccess == -1) return -1; //error with dequeue

			alarmToRun->alarm_handler; 
		}
	}

	return 0;
}

void alarm_system_initalize() {
	g_alarmsQueue = queue_new(); AbortGracefully(g_alarmsQueue == NULL, "Failed to initialize alarmsQueue in alarm_system_initalize()");
}

//creates an alarm given a delay, return 0 if success, return -1
int alarm_create(int delay) {
	//Validate input arguments and make sure our global alarms queue has been initialized
	if (delay < 0 || g_alarmsQueue == NULL) return -1;

	alarm_handler_t alarm_handler = alarm_get_alarm_handler();
	alarm_id newAlarm = register_alarm(delay, alarm_handler, NULL);
	if (newAlarm == NULL) return -1; //if our register_alarm returned NULL, return error status
	
	//add new alarm to global alarms queue, return error status if failed
	int sortedInsertSuccess = queue_sortedinsert(g_alarmsQueue, newAlarm, ((alarm_t*)newAlarm)->interrupt_to_wake);
	if (sortedInsertSuccess == -1) return -1;

	return 0;
}


/* see alarm.h */
alarm_id
register_alarm(int delay, alarm_handler_t alarm, void *arg) {
	//if delay period is invalid or alarm is null, return NULL
	if (delay < 0 || alarm == NULL) return NULL;
	assert(g_interruptCount >= 0); 
    
	alarm_t* newAlarm = malloc(sizeof(alarm_t)); //create a new alarm
	if (alarm == NULL) return NULL; //return NULL if malloc errored

	newAlarm->alarm_handler = alarm;
	//newAlarm->hasExecuted = false;
	newAlarm->sleepingThread = minithread_self(); //add the calling thread to the alarm

	//calculate at which global interrupt the alarm should go off at, round up so thread sleeps at least that amount
	uint64_t numInterruptsToSleep = (uint64_t) delay / 100; //divide delay by interrupt period to figure out how many ticks to sleep
	if (delay % 100 != 0) numInterruptsToSleep++; //add 1 tick if our delay was longer than 1 period
	newAlarm->interrupt_to_wake = g_interruptCount + numInterruptsToSleep;

	return newAlarm;
}

/* see alarm.h */
int
deregister_alarm(alarm_id alarm) {
	AbortGracefully(alarm == NULL, "Invalid input alarm in deregister_alarm()");
	assert(g_alarmsQueue != NULL);

	int alarmExecuted = queue_delete(g_alarmsQueue, alarm); //if alarm has executed, it would not be in the queue and queue_delete would return -1.
	return (alarmExecuted == -1); //return 1 if alarm has been excuted, 0 otherwise
}

/*
** vim: ts=4 sw=4 et cindent
*/
