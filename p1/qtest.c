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


void freeTest(queue_t* q){
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

assert( queue_dequeue(q,dptr)==0);//0
 int j=*((int*) d);
assert(j==4);

assert( queue_dequeue(q,dptr)==0);//0
 j=*((int*) d);
assert(j==2);

assert(queue_length(q)==0);


queue_prepend(q,n2);


queue_prepend(q,n4);

int e=5;
void* n5=&e;
queue_append(q,n5);//4,2,5//append
assert(queue_length(q)==3);

int g=6;
void* n6=&g;
queue_prepend(q,n6);//6,4,2,5
assert(queue_length(q)==4);

printf("iteration %d \n",queue_iterate(q, func, n6));//shud b 6,4,2,5
//test iterate, and free
free(q);
printf("iteration %d \n",queue_iterate(q, func, n6));//shud b 0 elements
 }



void iterateTest(queue_t* q){
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

assert( queue_dequeue(q,dptr)==0);//0
 int j=*((int*) d);
assert(j==4);

assert(queue_dequeue(q,dptr)==0);//0
 j=*((int*) d);
assert(j==2);

assert(queue_length(q)==0);


queue_prepend(q,n2);


queue_prepend(q,n4);

int e=5;
void* n5=&e;
queue_append(q,n5);//4,2,5//append
assert(queue_length(q)==3);

int g=6;
void* n6=&g;
queue_prepend(q,n6);//6,4,2,5
assert(queue_length(q)==4);

printf("iteration %d \n",queue_iterate(q, func, n6));//shud print elements 6,4,2,5
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
assert( queue_dequeue(q,dptr)==0);
int j=*((int*) d);
assert(j==4);

assert( queue_dequeue(q,dptr)==0);
 j=*((int*) d);
assert(j==2);

assert( queue_dequeue(q,dptr)==0);
 j=*((int*) d);
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
assert( queue_length(q)==1);//1
assert( queue_dequeue(q,dptr)==0);//0
int j=*((int*) d);
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

assert( queue_dequeue(q,dptr)==0);
 j=*((int*) d);
assert(j==4);

assert( queue_dequeue(q,dptr)==0);
 j=*((int*) d);
assert(j==2);


 	}



 	void cornercasesForEach(queue_t* q){
 		/*int other=10;
void* d=&other;
void** dptr=&d;*/
 		/*empty q, q with 1 element*, deleting element that doesnt exist, delete first item, delete last item*/
	int other=10;
void* d=&other;
void** dptr=&d;
 		/*empty q, q with 1 element*, deleting element that doesnt exist, delete first item, delete last item*/
/*
empty:
prepend 
dequeue
append
dequeue
dequeue
iterate
DELETE
free*/
int b=3;
void* n3=&b;
queue_prepend(q,n3);
assert(queue_length(q)==1);
assert( queue_dequeue(q,dptr)==0);
int j=*((int*) d);
assert(j==3);

int a=2;
void* n2=&a;
queue_prepend(q,n2);
assert(queue_length(q)==1);
assert( queue_dequeue(q,dptr)==0);
j=*((int*) d);
assert(j==2);
assert(queue_dequeue(q,dptr)==-1);
assert(queue_iterate(q, func, n3)==-1);
assert(queue_delete(q,n3)==-1);
free(q);
q=queue_new();
queue_append(q,n3);//3
/*
then new queue then
with 1 element:
append
dequeue
prepend
dequeue
iterate
delete
free*/
queue_append(q,n2);//3,2
assert( queue_dequeue(q,dptr)==0);
j=*((int*) d);
assert(j==3);
queue_prepend(q,n3);//3,2
assert( queue_dequeue(q,dptr)==0);
j=*((int*) d);
assert(j==3);
printf("iteration %d \n",queue_iterate(q, func, n2));//shud print that element is 2
queue_delete(q,n2);
assert(queue_length(q)==0);
assert(queue_dequeue(q,dptr)==-1);
assert(queue_length(q)==0);
queue_prepend(q,n3);
free(q);
//HOW TO TEST AFTER FREE?
q=queue_new();
queue_append(q,n3);
/*
then again new queue
append
prepend
append
delete item not existing
prepend
delete first item
delete last item
*/
queue_prepend(q,n2);//2,3
int e=4;
void* n4=&e;
queue_append(q,n4);//2,3,4
int nonexist=0;
void* pnonexist=&nonexist;
assert(queue_delete(q,pnonexist)==-1);
queue_prepend(q,pnonexist);//0,2,3,4
queue_delete(q,pnonexist);
queue_delete(q,n4);
assert(queue_length(q)==2);
printf("iteration %d \n",queue_iterate(q, func, n2));//should print elements 2,3

 	}






 int main(){
 	queue_t* q=queue_new();
 	assert( queue_length(q)==0);//0
freeTest(q);
//free(q);
q=queue_new();
iterateTest(q);
free(q);
q=queue_new();
appendANDprependANDdequeue_test(q);
free(q);
q=queue_new();
prependANDdequeueANDdelete_test(q);
free(q);
q=queue_new();
cornercasesForEach(q);




 	return 1;
 }

 