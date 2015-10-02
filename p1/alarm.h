#ifndef __ALARM_H__
#define __ALARM_H__ 1

/*
 * This is the alarm interface. You should implement the functions for these
 * prototypes, though you may have to modify some other files to do so.
 */


/* An alarm_handler_t is a function that will run within the interrupt handler.
 * It must not block, and it must not perform I/O or any other long-running
 * computations.
 */
typedef void (*alarm_handler_t)(void*);
typedef void *alarm_id;

/* Initializes the global variables for alarm
*/
void alarm_system_initalize();

/* Creates a new alarm with a given delay
*/
int alarm_create(int delay);

/* Runs any alarms that needs to be ran at the given interrupt count.
* Returns 0 if successful, -1 if any errors
*/
int alarm_run();

/* register an alarm to go off in "delay" milliseconds.  Returns a handle to
 * the alarm.
 */
alarm_id register_alarm(int delay, alarm_handler_t func, void *arg);

/* unregister an alarm.  Returns 0 if the alarm had not been executed, 1
 * otherwise.
 */
int deregister_alarm(alarm_id id);

#endif
