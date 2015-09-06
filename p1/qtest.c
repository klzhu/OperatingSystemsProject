/*
 * Generic queue implementation.
 *
 */
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

 int main(){
 	queue_t* q=queue_new();
 	printf("%d \n", queue_length(q));//0
 	/*methods to test: int queue_prepend(queue_t*, void*);

int queue_append(queue_t*, void*);


int queue_dequeue(queue_t*, void**);

typedef void (*func_t)(void*, void*);
int queue_iterate(queue_t*, func_t, void*);


int queue_free (queue_t*);

int queue_length(const queue_t* queue);

int queue_delete(queue_t* queue, void* item);*/
int i=1;
void* n=&i;
int other=10;
void* d=&other;
void** deq=&d;
queue_prepend(q,n);
printf("%d \n", queue_length(q));//1
printf("%d \n", queue_dequeue(q,deq));//0
void* dq=*deq;
int j=*((int*)dq);
printf("%d \n", j);//1
 	return 1;
 }