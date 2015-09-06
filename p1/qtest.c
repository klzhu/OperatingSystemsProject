/*
 * Generic queue implementation.
 *
 */
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

 int main(){
 	queue_t* q=queue_new();
 	printf("%d \n", queue_length(q));
 	return 1;
 }