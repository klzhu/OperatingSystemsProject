#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

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
queue_t* g_alarmsQueue = NULL; //global queue that holds our alarms in sorted order according to when they should be set off

//struct for our alarm
typedef struct alarm {
	uint64_t interrupt_to_wake; //tracks at which global interrupt the alarm should go off
	minithread_t* sleepingThread; //handler to our sleeping thread for this alarm
	bool hasExecuted; //tracks whether alarm has executed or not
	alarm_handler_t alarm_handler; //method to execute when alarm goes off
	//semaphore_t* alarmSem; //semaphore to hold the waiting threads for this alarm
} alarm_t;

//alarm handler
int alarm_handler(arg_t arg) {
	assert(g_alarmsQueue != NULL); //global alarms queue should be initialized

	//handle alarms

	return -1;
}

void alarm_system_initalize() {
	g_alarmsQueue = queue_new(); AbortGracefully(g_alarmsQueue == NULL, "Failed to initialize alarmsQueue in alarm_system_initalize()");
}

//creates an alarm given a delay, return 0 if success, return -1
int alarm_create(int delay) {
	//Validate input arguments and make sure our global alarms queue has been initialized
	if (delay < 0 || g_alarmsQueue == NULL) return -1;

	alarm_id newAlarm = register_alarm(delay, alarm_handler, NULL);
	if (newAlarm == NULL) return -1; //if our register_alarm returned NULL, return error status
	
	//add new alarm to global alarms queue, return error status if failed
	int appendSuccess = queue_append(g_alarmsQueue, newAlarm);
	if (appendSuccess == -1) return -1;

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
	newAlarm->hasExecuted = false;
	newAlarm->sleepingThread = minithread_self(); //add the calling thread to the alarm

	//calculate at which global interrupt the alarm should go off at, round up so thread sleeps at least that amount
	uint64_t numInterruptsToSleep = ceil(delay / 100); //divide delay by interrupt period to figure out how many ticks to sleep
	newAlarm->interrupt_to_wake = g_interruptCount + numInterruptsToSleep;

	return newAlarm;
}

/* see alarm.h */
int
deregister_alarm(alarm_id alarm) {
	AbortGracefully(alarm == NULL, "Invalid input alarm in deregister_alarm()");
	assert(g_alarmsQueue != NULL);

	alarm_id alarmFound = NULL;
	bool alarmExecuted = false;
	queue_search(g_alarmsQueue, alarm, &alarmFound);
	
	if (alarmFound == NULL) return -1; //we didn't find the alarm
	else alarmExecuted = ((alarm_t*)alarmFound)->hasExecuted;

	int deletionSuccess = queue_delete(g_alarmsQueue, alarm);
	AbortGracefully(deletionSuccess == -1, "queue_delete failed in deregister_alarm()");
    
	return (alarmExecuted); //return 1 if alarm has been excuted, 0 otherwise
}

/*
** vim: ts=4 sw=4 et cindent
*/
