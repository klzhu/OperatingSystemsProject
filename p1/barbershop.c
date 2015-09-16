/*
 * barbershop.c:
 *      Your comments go here
 *      
 *
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include "minithread.h"
#include "queue.h"
#include "synch.h"

#include <assert.h>


 /*
 A barbershop holds up to k customers, and M barbers work on the customers. Each customer has a specific barber that they want to have their hair cut by.
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
 typedef struct barber{
	int barberId;
	semaphore_t* barberBusy;
	//minithread_t* barberThread; maybe not needed
}barber;
typedef struct customer{
	int customerId;
	barber preferredBarber;//int preferredBarberId??
    //minithread_t* customerThread;
}customer;

int getHaircut(customer c,semaphore_t* numAllowed){
	numAllowed.P();
	//select preferred barber--HOW? of the free barbers check their ids?
	//if not busy
	//call barber to cutHair 
	     //and after done stop itself..or like do whatever there is to signal that this thread is done
	//if busy then yield--call barbers semaphore first try p then do v
	numAllowed.V();
return 0;

}

int cutHair(barber b, int called){

	b->barberBusy.P();
	//int called specifies if barber was called by a customer(1) but if 0 barber can go to find customer
//set barber busy
//cut hair
//set barber not busy, and return
	b->barberBusy.V();
	return 0;
}

int main(void) {
int k=0;//shop sapce(wait room)
int M=0;//barbers
int N=0;//total customers
semaphore_t* shopRoom=semaphore_create();
semaphore_initialize(k);
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
//when creating customers, have pointer to preferred barber thread?--but then need like array of barbers first--whatever, ill just do it with array

  printf("The barbershop is open for business!\n");
  return 0;
}


