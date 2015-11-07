/*
 *  Implementation of minimsgs and miniports.
 */
#include<stdbool.h>
#include<assert.h>
#include<string.h>

#include "minimsg.h"
#include "queue.h"
#include "synch.h"
#include "defs.h"
#include "interrupts.h"
#include "miniheader.h"

// ---- Constants ---- //
#define BOUNDED_PORT_START		32768	/* The beginning port number for bounded port */
#define BOUNDED_PORT_END		65535   /* The end port number for bounded */
#define UNBOUNDED_PORT_START	0		/* The beginning port number for unbounded port */
#define UNBOUNDED_PORT_END		32767	/* The end port number for unbounded port */

// ---- Global Variables ---- //
int g_boundPortCounter = -1; //for incrementally assigning bounded ports
bool g_boundedPortAvail[BOUNDED_PORT_END - BOUNDED_PORT_START + 1]; //track availability of bounded ports once g_boundPortCounter goes above max value
semaphore_t* g_semaBoundLock = NULL; // used as mutex to protect access to g_boundPortCounter & g_boundedPortAvail

miniport_t* g_unboundedPortPtrs[UNBOUNDED_PORT_END - UNBOUNDED_PORT_START + 1]; //tracks the pointers to all of our unbounded ports
semaphore_t* g_semaUnboundLock = NULL; // used as mutex to protect modification to g_unboundedPortPtrs

struct miniport
{
	char port_type; //'b' indicates bounded port, 'u' indicates unbounded port
	int port_number; 

	union {
		struct unbounded {
			queue_t *incoming_data;
			semaphore_t *datagrams_ready;
		} unbound_port;
		struct bound {
			network_address_t remote_addr;
			int remote_unbound_port;
		} bound_port;
	};
};

void
minimsg_initialize()
{
	g_boundPortCounter = BOUNDED_PORT_START; //bounded ports range from 32768 - 65535
	memset(g_boundedPortAvail, 1, sizeof(g_boundedPortAvail)); //initialize array element to true, every port is avail when we initialize
	memset(g_unboundedPortPtrs, 0, sizeof(g_unboundedPortPtrs)); //set array of unbounded port pointers to null
	g_semaBoundLock = semaphore_create(); AbortOnCondition(g_semaBoundLock == NULL, "g_semaBoundLock failed in minimsg_initialize()");
	g_semaUnboundLock = semaphore_create(); AbortOnCondition(g_semaUnboundLock == NULL, "g_semaUnboundLock failed in minimsg_initialize()");
	semaphore_initialize(g_semaBoundLock, 1); //init sema to 1 (available).
	semaphore_initialize(g_semaUnboundLock, 1); //init sema to 1 (available).
}

miniport_t*
miniport_create_unbound(int port_number)
{
	assert(g_boundPortCounter >= 0); //sanity check to ensure minimsg_initialize() has been called first

	//ensure that port_number is valid
	if (port_number < UNBOUNDED_PORT_START || port_number > UNBOUNDED_PORT_END) return NULL;

	semaphore_P(g_semaUnboundLock); //critical section to modify global g_unboundedPortPtrs
	if (g_unboundedPortPtrs[port_number] == NULL) { //if the unbounded port has not been created, if it has been created, skip this and return reference
	//if not, we must create the unbounded port
		miniport_t* u_miniport = malloc(sizeof(miniport_t));
		if (u_miniport == NULL) //malloc errored
		{
			semaphore_V(g_semaUnboundLock);
			return NULL;
		}
		u_miniport->port_number = port_number;
		u_miniport->port_type = 'u';

		//create necessary semaphore and queue for unbounded miniport
		u_miniport->unbound_port.datagrams_ready = semaphore_create();
		if (u_miniport->unbound_port.datagrams_ready == NULL) //error creating our sema
		{
			free(u_miniport); //free newly allocated space for miniport
			semaphore_V(g_semaUnboundLock);
			return NULL;
		}

		u_miniport->unbound_port.incoming_data = queue_new();
		if (u_miniport->unbound_port.incoming_data == NULL) //error creating our queue
		{
			semaphore_destroy(u_miniport->unbound_port.datagrams_ready); //free newly allocated space for sema
			free(u_miniport); //free newly allocated space for miniport
			semaphore_V(g_semaUnboundLock);
			return NULL;
		}

		semaphore_initialize(u_miniport->unbound_port.datagrams_ready, 0); //initialize our waiting sema
		g_unboundedPortPtrs[port_number] = u_miniport; //update our array of pointers for our unbounded ports
	}

	semaphore_V(g_semaUnboundLock); //end of critical session
    return g_unboundedPortPtrs[port_number];
}

miniport_t*
miniport_create_bound(const network_address_t addr, int remote_unbound_port_number)
{
	assert(g_boundPortCounter >= 0); //sanity check to ensure minimsg_initialize() has been called first

	//validate input, unbound port number should be between 0 - 32767
	if (addr == NULL || remote_unbound_port_number < UNBOUNDED_PORT_START || remote_unbound_port_number > UNBOUNDED_PORT_END) return NULL;

	miniport_t* b_miniport = malloc(sizeof(miniport_t));
	if (b_miniport == NULL) return NULL; //malloc errored

	b_miniport->port_type = 'b'; //set miniport type to bounded
	b_miniport->bound_port.remote_unbound_port = remote_unbound_port_number;
	network_address_copy(addr, b_miniport->bound_port.remote_addr);

	semaphore_P(g_semaBoundLock); //critical section to access global variables g_boundPortCounter & g_boundedPortAvail
	if (g_boundPortCounter > BOUNDED_PORT_END) //if we've reached the end of our port space, we need to search for an available port number
	{
		int k = 0;
		while (k <= BOUNDED_PORT_END - BOUNDED_PORT_START && g_boundedPortAvail[k] == false)
			k++;	// find the first element equaling true

		if (k > BOUNDED_PORT_END - BOUNDED_PORT_START) { //if no port is available
			free(b_miniport);	// free the newly created bound port
			b_miniport = NULL;	// will return NULL later
		}
		else { // found an available port
			b_miniport->port_number = k + BOUNDED_PORT_START; // set our miniport num to the available port num
			g_boundedPortAvail[k] = false; //set the port to be used
		}
	}
	else //otherwise, set our port number to g_boundPortCounter
	{
		b_miniport->port_number = g_boundPortCounter;
		g_boundedPortAvail[g_boundPortCounter - BOUNDED_PORT_START] = false; //set the avail of that port to false
		g_boundPortCounter++; //increment counter
	}

	semaphore_V(g_semaBoundLock); //end of critical section

	return b_miniport;
}

void
miniport_destroy(miniport_t* miniport)
{
	assert(g_boundPortCounter >= 0); //sanity check to ensure minimsg_initialize() has been called first

	//validate input
	AbortOnCondition(miniport == NULL, "Null argument miniport in miniport_destroy()");

	//check if unbounded port, if so, we must free our queue and sema
	if (miniport->port_type == 'u') //if unbounded port
	{
		assert(miniport->unbound_port.datagrams_ready != NULL && miniport->unbound_port.incoming_data != NULL
			&& miniport->port_number >= UNBOUNDED_PORT_START && miniport->port_number <= UNBOUNDED_PORT_END); //self check

		//update our global array of unbounded ports first
		semaphore_P(g_semaUnboundLock); // critical session
		g_unboundedPortPtrs[miniport->port_number] = NULL;
		semaphore_V(g_semaUnboundLock); //end of critical session

		//free our queue
		int queueFreeSuccess = queue_free_nodes_and_queue(miniport->unbound_port.incoming_data);
		AbortOnCondition(queueFreeSuccess == -1, "Queue_free failed in miniport_destroy()");

		//free semaphore
		semaphore_destroy(miniport->unbound_port.datagrams_ready);
	}
	else //if bounded port
	{
		assert(miniport->port_number >= BOUNDED_PORT_START && miniport->port_number <= BOUNDED_PORT_END);
		semaphore_P(g_semaBoundLock); //begin critical section
		g_boundedPortAvail[miniport->port_number - BOUNDED_PORT_START] = true; //set the avail of our bounded port to true
		semaphore_V(g_semaBoundLock); //end critical section
	}

	free(miniport);
}

int
minimsg_send(miniport_t* local_unbound_port, const miniport_t* local_bound_port, const char* msg, int len)
{
	assert(g_boundPortCounter >= 0); //sanity check to ensure minimsg_initialize() has been called first

	//validate input
	if (local_unbound_port == NULL || local_bound_port == NULL || msg == NULL || len < 0 || len > MINIMSG_MAX_MSG_SIZE) return -1;

	//generate the header
	mini_header_t header;
	header.protocol = PROTOCOL_MINIDATAGRAM; //set protocol type
	network_address_t my_address;
	network_get_my_address(my_address);
	pack_address(header.source_address, my_address);
	pack_unsigned_short(header.source_port, (unsigned short)local_unbound_port->port_number);
	pack_address(header.destination_address, local_bound_port->bound_port.remote_addr);
	pack_unsigned_short(header.destination_port, (unsigned short)local_bound_port->bound_port.remote_unbound_port);
	
	//send message now
	int sentBytes = network_send_pkt(local_bound_port->bound_port.remote_addr, sizeof(header), (char*)&header, len, msg);

	if (sentBytes == -1) return -1; //we failed to send our message
	else return sentBytes - sizeof(header); //else return size of our message not inclusive of header
}

int
minimsg_receive(miniport_t* local_unbound_port, miniport_t** new_local_bound_port, char* msg, int *len)
{
	assert(g_boundPortCounter >= 0); //sanity check to ensure minimsg_initialize() has been called first

	//validate input
	if (new_local_bound_port == NULL || local_unbound_port == NULL|| msg == NULL || len == NULL || *len < 0
		|| local_unbound_port->port_type != 'u') return -1;

	assert(local_unbound_port->unbound_port.datagrams_ready != NULL && local_unbound_port->unbound_port.datagrams_ready != NULL);

	semaphore_P(local_unbound_port->unbound_port.datagrams_ready); //P the semaphore, if the count is 0 we're blocked until packet arrives

	//once a packet arrives and we've woken
	network_interrupt_arg_t* dequeuedPacket = NULL;
	interrupt_level_t old_level = set_interrupt_level(DISABLED); // critical session (to dequeue the packet queue)
	assert(queue_length(local_unbound_port->unbound_port.incoming_data) > 0); //sanity check - our queue should have a packet waiting
	int dequeueSuccess = queue_dequeue(local_unbound_port->unbound_port.incoming_data, (void**)&dequeuedPacket);
	set_interrupt_level(old_level); //end of critical session to restore interrupt level
	AbortOnCondition(dequeueSuccess != 0, "Queue_dequeue failed in minimsg_receive()");

	//get our header and message from the dequeued packet
	mini_header_t *receivedHeaderPtr = (mini_header_t*)dequeuedPacket->buffer;
	//set *len to the msg length to be copied: if the length of the message received is >= *len, no change to *len. Otherwise, set *len to the length of our received message
	if (dequeuedPacket->size - sizeof(mini_header_t) < *len) *len = dequeuedPacket->size - sizeof(mini_header_t);
	memcpy(msg, dequeuedPacket->buffer + sizeof(mini_header_t), *len); // msg is after header

	//create our new local bound port pointed back to the sender
	int sourcePort = (int)unpack_unsigned_short(receivedHeaderPtr->source_port);	// get source's listening port
	assert(sourcePort >= UNBOUNDED_PORT_START && sourcePort <= UNBOUNDED_PORT_END); //make sure source port num is valid
	network_address_t remoteAddr;
	unpack_address(receivedHeaderPtr->source_address, remoteAddr);	// get source's network address
	free(dequeuedPacket); // release the memory allocated to the packet

	*new_local_bound_port = miniport_create_bound(remoteAddr, sourcePort);	// create a bound port
	if (*new_local_bound_port == NULL) return -1;

    return *len; //return data payload bytes received not inclusive of header
}

/* Network handler handles being interrupted when a packet arrives. It will create the unbounded listening port
*  if it has not been created already. It will then enqueue the packet and V the count semaphore and wake up
*  a waiting thread if any. Our common network handler which calls this has already disabled interrupts for us.
*/
void 
minimsg_network_handler(network_interrupt_arg_t* arg)
{
	//Get header and destination port
	mini_header_t *receivedHeaderPtr = (mini_header_t*)arg->buffer;
	int destPort = (int)unpack_unsigned_short(receivedHeaderPtr->destination_port);

	//if dest port is invalid or the unbounded port has not been initialized, throw away the packet
	if (destPort < UNBOUNDED_PORT_START || destPort > UNBOUNDED_PORT_END || g_unboundedPortPtrs[destPort] == NULL)
	{
		free(arg);
		return;
	}

	//queue the packet and V the semaphore
	assert(g_unboundedPortPtrs[destPort]->port_type == 'u' && g_unboundedPortPtrs[destPort]->unbound_port.datagrams_ready != NULL
		&& g_unboundedPortPtrs[destPort]->unbound_port.incoming_data != NULL); 
	int appendSuccess = queue_append(g_unboundedPortPtrs[destPort]->unbound_port.incoming_data, (void*)arg);
	AbortOnCondition(appendSuccess == -1, "Queue_append failed in minimsg_network_handler()");

	semaphore_V(g_unboundedPortPtrs[destPort]->unbound_port.datagrams_ready);
}
