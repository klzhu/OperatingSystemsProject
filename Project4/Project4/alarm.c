#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include "interrupts.h"
#include "alarm.h"
#include "minithread.h"
#include "queue.h"
#include "synch.h"
#include "common.h"

// ---- Global variables ---- //
extern const int INTERRUPT_PERIOD_IN_MILLISECONDS; //clock interrupt period in milliseconds
extern uint64_t g_interruptCount; //global counter to count how many interrupts has passed. This value should not overflow for years.
queue_t* g_alarmsQueue = NULL; //global queue that holds our alarms in sorted order according to when they should be set off
uint64_t g_smallestInterruptToWake = 0; //Tracks the first item's interruptToWake in g_alarmsQueue

//struct for our alarm
typedef struct alarm {
	uint64_t interruptToWake; //tracks at which global interrupt the alarm should go off
	void* alarmHandlerArg; //argument to alarm handler
	alarm_handler_t alarmHandler; //method to execute when alarm goes off
} alarm_t;

/* see alarm.h */
alarm_id
register_alarm(int delay, alarm_handler_t alarm, void *arg) {
	//if delay period is invalid or alarm is null, return NULL
	if (delay < 0 || alarm == NULL) return NULL;
    
	alarm_t* newAlarm = malloc(sizeof(alarm_t)); //create a new alarm
	if (alarm == NULL) return NULL; //return NULL if malloc errored

	newAlarm->alarmHandler = alarm;
	newAlarm->alarmHandlerArg = arg; 

	//calculate at which global interrupt the alarm should go off at, round up so thread sleeps at least that amount
	uint64_t numInterruptsToSleep = (delay + INTERRUPT_PERIOD_IN_MILLISECONDS / 2) / INTERRUPT_PERIOD_IN_MILLISECONDS; // # of interrupts, round to closest int 
	if (numInterruptsToSleep == 0) numInterruptsToSleep++; // make sure it delay at least one interrupt
	newAlarm->interruptToWake = g_interruptCount + numInterruptsToSleep;

	//disable interrupts as we begin access of global vars
	interrupt_level_t old_level = set_interrupt_level(DISABLED);

	//if this is first alarm being added, initialize alarms queue
	if (g_alarmsQueue == NULL) {
		g_alarmsQueue = queue_new();
		AbortOnCondition(g_alarmsQueue == NULL, "Failed to initialize alarms queue in register_alarm()");
	}

	//insert alarm into global alarms queue
	int insertSuccess = queue_ordered_insert(g_alarmsQueue, newAlarm, ((alarm_t*)newAlarm)->interruptToWake);
	if (insertSuccess != 0) { //insertion failed, free alarm and set newAlarm = NULL so we return NULL
		free(newAlarm);
		newAlarm = NULL;
	}

	//update g_smallestInteruptToWake if needed
	if (g_smallestInterruptToWake > newAlarm->interruptToWake)
		g_smallestInterruptToWake = newAlarm->interruptToWake;

	//restore interrupts to old level as we exit critical section
	set_interrupt_level(old_level);

	return newAlarm;
}

/* see alarm.h */
int
deregister_alarm(alarm_id alarm) {
	if (alarm == NULL || g_alarmsQueue == NULL) return -1; //if error, return -1

	//disable interrupts as we access our global queue
	interrupt_level_t old_level = set_interrupt_level(DISABLED);
	int alarmExecuted = queue_delete(g_alarmsQueue, alarm); //if alarm has executed, it would not be in the queue and queue_delete would return -1.
	set_interrupt_level(old_level); //restore interrupts as we leave crit section

	return (alarmExecuted == -1); //return 1 if alarm has been excuted, 0 otherwise
}

/* Checks the alarms and sets off those scheduled to go off.
* Returns 0 if successful, -1 if any errors
*/
int 
alarm_check_and_run() {
	assert(g_interruptCount > 0); //self check

	//disable interrupts as we begin access of our global variables
	interrupt_level_t old_level = set_interrupt_level(DISABLED);

	//if no alarms have been added or the smallest interrupt to wake has not been reached, do nothing
	if (g_alarmsQueue == NULL || g_smallestInterruptToWake > g_interruptCount) {
		set_interrupt_level(old_level);
		return 0;
	}

	while (queue_length(g_alarmsQueue) > 0) { //while there's an alarm in our queue
		alarm_t* currAlarm = NULL;
		int peekSuccess = queue_peek(g_alarmsQueue, (void**)&currAlarm); //peek at the first item in our queue
		if (peekSuccess == -1) {
			set_interrupt_level(old_level);
			return -1; //peek function failed to view first alarm
		}
		
		assert(currAlarm != NULL); //self check
		if (currAlarm->interruptToWake > g_interruptCount) {
			g_smallestInterruptToWake = currAlarm->interruptToWake; //set the next smallest interrupt to wake to the first elem in our queue
			break; //if first alarm in our queue is not set to go off, neither are rest of alarms
		}
		else { //first alarm is scheduled to go off
			int dequeueSuccess = queue_dequeue(g_alarmsQueue, (void**)&currAlarm); //remove first alarm from queue
			if (dequeueSuccess == -1) return -1; //failed to dequeue
			currAlarm->alarmHandler(currAlarm->alarmHandlerArg); //call alarm's alarm handler
			free(currAlarm); 
		}
	}

	set_interrupt_level(old_level); //restore interrupt level
	return 0;
}

/*
** vim: ts=4 sw=4 et cindent
*/
