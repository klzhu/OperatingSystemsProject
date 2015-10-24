/*
 * barbershop.c:
 *      Your comments go here
 *     A barbershop holds up to k customers, and M barbers work on the customers. Each customer has a specific barber that they want to have their hair cut by.
  You should implement this for any N customers, M barbers, and shop of size k (each of them >=1).
  while N create customers,
  while M create barbers
  shop size k...=to num barbers i assume...no, they say theres waiting area in shop

Correctness criteria are:

no customers should enter the barbershop unless there is room for them inside
a barber should cut hair if there is a waiting customer
customers should enter the barbershop if there is room inside
each barber and customer should reside in a separate minithread
Because the store owner wants to satisfy all their customers, people should be served FIFO but only by the barber of their choice.

customers= threads wanting to have haircut
barbers=threads cutting hair;
 */

//customer check if barber free,if yes grab that barber and switch to that barber ,if not then yield to next customer....so this is customers, but must give barbers time to do their work too
//barber: check if customer present, if yes do haircut then yield, if no then yield
//sequence: customer,barber,customer,barber...that wraps around...NO: let as many customers in as there are barbers, then sort which wants which barber and if >1 want same 
//aka if barber already taken then call yield and make the later one wait and let another customer in: so almost every time the shop is full
//so 1. let customers enter to fill shop
//2. match them with barbers
//3. if conflict, make some wait--- and let others in(if theres room)
//4. have barbers cut hair(once shop full or no customers left to enter)
//4.5 sequentially, once done, they can put customers in zombie queue
//5. each time barber done, a customer is let in
//6. check if barber for that customer available, if not then yield to next customer and so on
// FCFS to get into the shop. Then FCFS for each barber
//In your code, each customer should have a desired barber.  All this should be implemented with semaphores.  You shouldn't need explicit queues of threads.


//semaphore, initialize to k, decrement(P) as customer enters
//increment(V) as customer leaves
//1.fill shop as much as can
//2.have barbers do work in turn
//when a barber done: send finished customer to done state , signal for another customer to enter
//once that customer enters, if matched with barber proceed to cut hair and switch to next barber
//but if not...do we not let anyone in until customer gets his barber?...so like if everyone in waiting room waiting for 1 barber...is bad but in this case they would not let anyone else in
//1. create all barbers and customers, use fork, in the order we want them to execute
//preferred:
 //time_t t; 
   /* Intializes random number generator */
 //srand((unsigned) time(&t));
 //use by rand()%M;
//1 fork barbers:create barber, create thread with barber and cutHair as arg
//2 fork customers
//3 start customers
//if barber not busy then start barber-OR customer starts barber , **so customer comes makes barber work after barber done customer also done**
//create barbers

 
#include <stdlib.h>
#include <stdio.h>
#include "minithread.h"
#include "queue.h"
#include "synch.h"

#include <assert.h>


 
 
typedef struct barber{
	int barberId;
	semaphore_t* barberBusy;
}barber;
typedef struct customer{
	int customerId;
	barber* preferredBarber;
	minithread_t* preferredBarberT;
}customer;

semaphore_t* shopRoom=NULL;

int getHaircut(customer* c){
	if (c == NULL) return -1;
	
	printf("request cut");
	semaphore_P(shopRoom);

	semaphore_P(c->preferredBarber->barberBusy);
	minithread_start(c->preferredBarberT);
	semaphore_V(shopRoom);
	
return 0;
}

int cutHair(barber* b){//int called){ how to pass 2 args
	printf("cut hair");
	//semaphore_P(b->barberBusy);
	//int called specifies if barber was called by a customer(1) but if 0 barber can go to find customer
//set barber busy
//cut hair
//set barber not busy, and return
	int hair=12;
	hair=hair/2;
	semaphore_V(b->barberBusy);
	//semaphore_V(b->barberBusy);
	return 0;
}

int runShop(){
shopRoom =semaphore_create();
int k=5;//shop sapce(wait room)
int M=5;//barbers
int N=5;//total customers
//k spots in shop
semaphore_initialize(shopRoom,k);
//shop that holds barbers
minithread_t* allBarberThreads[M];
barber* allBarbers[M];
//create barbers to fill shop
int i=0;
while(i<M){
	barber* b=malloc(sizeof(barber));
	b->barberId=i;
	b->barberBusy=semaphore_create();
	semaphore_initialize(b->barberBusy,1);
	allBarberThreads[i]=minithread_create((proc_t)cutHair,(arg_t)b);
	allBarbers[i]=b;
	i+=1;
}


  printf("The barbershop is open for business!\n");
  //printf("before create customer");
  //create customers
  i=0;
  while(i<N){
  	//printf("create customer1");
	customer* c=malloc(sizeof(customer));
	c->customerId=i;
	time_t t; 
	srand((unsigned) time(&t));
	int r=rand()%M;
	//printf("create customer2");
	c->preferredBarber=allBarbers[r];
	c->preferredBarberT=allBarberThreads[r];
	//printf("create customer");
	minithread_fork((proc_t)getHaircut,(arg_t)c);
i+=1;
}
printf("finished while");
return 0;
}

int main(int argc, char * argv[]) {
  minithread_system_initialize(runShop, NULL); //start system
  return 0;
}


