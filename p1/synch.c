#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "defs.h"
#include "synch.h"
#include "queue.h"
#include "minithread.h"

/*
 *      You must implement the procedures and types defined in this interface.
 */


/*
 * Semaphores.
 */
struct semaphore {
    /* This is temporary so that the compiler does not error on an empty struct.
     * You should replace this with your own struct members.
     */
    int tmp;
};


semaphore_t* semaphore_create() {
    return NULL;
}

void semaphore_destroy(semaphore_t *sem) {
}

void semaphore_initialize(semaphore_t *sem, int cnt) {
}

void semaphore_P(semaphore_t *sem) {
}

void semaphore_V(semaphore_t *sem) {
}
