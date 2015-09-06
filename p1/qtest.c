/*
 * Generic queue implementation.
 *
 */
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>
 #include <assert.h>

 //void (*func_t)(void*, void*)
 void func (void* x, void* t){
 	printf("element: %d \n", *((int*)x));
 }

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
int other=10;
void* d=&other;
void** dptr=&d;

int a=2;
void* n2=&a;
queue_prepend(q,n2);

int b=3;
void* n3=&b;
queue_prepend(q,n3);

int c=4;
void* n4=&c;
queue_prepend(q,n4);

queue_delete(q,n3);//now 4,2

printf("%d \n", queue_dequeue(q,dptr));//0
 int j=*((int*) d);
printf("%d \n", j);//1
assert(j==4);

printf("%d \n", queue_dequeue(q,dptr));//0
 j=*((int*) d);
printf("%d \n", j);//1
assert(j==2);

assert(queue_length(q)==2);

int e=5;
void* n5=&e;
queue_append(q,n5);//4,2,5
assert(queue_length(q)==3);

int g=6;
void* n6=&g;
queue_prepend(q,n6);//6,4,2,5
assert(queue_length(q)==4);

printf("iteration %d \n",queue_iterate(q, func, n6));
//test iterate, and free



 	return 1;
 }

 void appendANDprependANDdequeue_test(queue_t* q){

int other=10;
void* d=&other;
void** dptr=&d;


 	int a=2;
void* n2=&a;
queue_append(q,n2);

int b=3;
void* n3=&b;
queue_append(q,n3);
int c=4;
void* n4=&c;
queue_prepend(q,n4);
//q shud b 4,2,3
printf("%d \n", queue_dequeue(q,dptr));
int j=*((int*) d);
printf("%d \n", j);
assert(j==4);

printf("%d \n", queue_dequeue(q,dptr));
 j=*((int*) d);
printf("%d \n", j);
assert(j==2);

printf("%d \n", queue_dequeue(q,dptr));
 j=*((int*) d);
printf("%d \n", j);
assert(j==3);

 }




 void prependANDdequeueANDdelete_test(queue_t* q){
 	int i=1;
void* n=&i;
int other=10;/*
void* d=&other;*/
queue_prepend(q,n);
void* d=&other;
void** dptr=&d;
printf("%d \n", queue_length(q));//1
printf("%d \n", queue_dequeue(q,dptr));//0
int j=*((int*) d);
printf("%d \n", j);//1
assert(j==1);
//printf("%d \n", queue_dequeue(q,dptr));// will return -1 in future
int a=2;
void* n2=&a;
queue_prepend(q,n2);

int b=3;
void* n3=&b;
queue_prepend(q,n3);

int c=4;
void* n4=&c;
queue_prepend(q,n4);

queue_delete(q,n3);//now 4,2

printf("%d \n", queue_dequeue(q,dptr));//0
 j=*((int*) d);
printf("%d \n", j);//1
assert(j==4);

printf("%d \n", queue_dequeue(q,dptr));//0
 j=*((int*) d);
printf("%d \n", j);//1
assert(j==2);


 	}



 	void cornercasesForEach(queue_t* q){
 		/*int other=10;
void* d=&other;
void** dptr=&d;*/
 		/*empty q, q with 1 element*, deleting element that doesnt exist, delete first item, delete last item*/

 	}