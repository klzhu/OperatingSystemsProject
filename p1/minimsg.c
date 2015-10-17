/*
 *  Implementation of minimsgs and miniports.
 */
#include <stdbool.h>
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
miniport_t* g_unboundedPortPtrs[UNBOUNDED_PORT_END - UNBOUNDED_PORT_START + 1]; //tracks the pointers to all of our unbounded ports

struct miniport
{
	char port_type; //b indicates bounded port, u indicates unbounded port
	int port_number; 

	union {
		struct unbounded {
			queue_t *incoming_data;
			semaphore_t *datagrams_ready;
		} unbound_t;
		struct bound {
			network_address_t remote_addr;
			int remote_unbound_port;
		} bound_t;
	};
};

void
minimsg_initialize()
{
	g_boundPortCounter = BOUNDED_PORT_START; //bounded ports range from 32768 - 65535
	memset(g_boundedPortAvail, 1, sizeof(g_boundedPortAvail)); //initialize array element to true, every port is avail when we initialize
	int i;
	for (i = 0; i <= UNBOUNDED_PORT_END; i++) //set array of unbounded port pointers to null
	{
		g_unboundedPortPtrs[i] = NULL;
	}
}

miniport_t*
miniport_create_unbound(int port_number)
{
	assert(g_boundPortCounter >= 0); //sanity check to ensure minimsg_initialize() has been called first

	//ensure that port_number is valid
	if (port_number < UNBOUNDED_PORT_START || port_number > UNBOUNDED_PORT_END) return NULL;

	//if the unbounded port has already been created, return reference to that port
	if (g_unboundedPortPtrs[port_number] != NULL) return g_unboundedPortPtrs[port_number];

	//if not, we must create the unbounded port
	miniport_t* u_miniport = malloc(sizeof(miniport_t));
	if (u_miniport == NULL) return NULL; //malloc errored

	//create necessary semaphore and queue for unbounded miniport
	semaphore_t* datagrams_ready = semaphore_create();
	if (datagrams_ready == NULL) //error creating our sema
	{
		//free newly allocated space for miniport, return null
		free(u_miniport);
		return NULL;
	}

	queue_t* incoming_data = queue_new();
	if (incoming_data == NULL) //error creating our queue
	{
		//free newly allocated space for miniport and allocated space for sema, return NULL
		free(datagrams_ready);
		free(u_miniport);
		return NULL;
	}

	semaphore_initialize(datagrams_ready, 0); //initialize our waiting sema

	//set our unbounded miniport
	u_miniport->port_number = port_number;
	u_miniport->port_type = 'u';
	u_miniport->unbound_t.datagrams_ready = datagrams_ready;
	u_miniport->unbound_t.incoming_data = incoming_data;

	//update our array of pointers for our unbounded ports MUST ADD THIS TO CRIT SECTION
	g_unboundedPortPtrs[port_number] = u_miniport;

    return u_miniport;
}

miniport_t*
miniport_create_bound(network_address_t addr, int remote_unbound_port_number)
{
	assert(g_boundPortCounter >= 0); //sanity check to ensure minimsg_initialize() has been called first

	//validate input, unbound port number should be between 0 - 32767
	if (addr == NULL || remote_unbound_port_number < UNBOUNDED_PORT_START || remote_unbound_port_number > UNBOUNDED_PORT_END) return NULL;

	miniport_t* b_miniport = malloc(sizeof(miniport_t));
	if (b_miniport == NULL) return NULL; //malloc errored

	//disable interrupts as we set the miniport number and increment global counter
	interrupt_level_t old_level = set_interrupt_level(DISABLED);
	if (g_boundPortCounter > BOUNDED_PORT_END) //if we've reached the end of our port space, we need to search for an available port number
	{
		int i;
		int freePortNum = -1;
		for (i = 0; i < BOUNDED_PORT_START; i++)
		{
			if (g_boundedPortAvail[i] == true)
			{
				freePortNum = i + BOUNDED_PORT_START; //if we found an available port, set out port number to that
				g_boundedPortAvail[i] = false; //set it to false because we're going to use it
				break;
			}
		}

		if (freePortNum == -1) return NULL; //if we didn't find an available port, return NULL
		else b_miniport->port_number = freePortNum; //otherwise, set our miniport num to the available port num
	}
	else //otherwise, set our port number to g_boundPortCounter
	{
		b_miniport->port_number = g_boundPortCounter;
		g_boundedPortAvail[g_boundPortCounter - BOUNDED_PORT_START] = false; //set the avail of that port to false
		g_boundPortCounter++; //increment counter
	}

	set_interrupt_level(old_level); //restore interrupt level

	b_miniport->port_type = 'b'; //set miniport type to bounded
	b_miniport->bound_t.remote_unbound_port = remote_unbound_port_number;
	b_miniport->bound_t.remote_addr[0] = addr[0];
	b_miniport->bound_t.remote_addr[1] = addr[1];

	return b_miniport;
}

void
miniport_destroy(miniport_t* miniport)
{
	assert(g_boundPortCounter >= 0); //sanity check to ensure minimsg_initialize() has been called first

	//validate input
	AbortOnCondition(miniport == NULL, "Null argument miniport in miniport_destroy()");

	//check if unbounded port, if so, we must free our queue and sema
	if (miniport->port_type == 'u')
	{
		assert(miniport->unbound_t.datagrams_ready != NULL && miniport->unbound_t.incoming_data != NULL); //self check

		//free our queue
		int queueFreeSuccess = queue_free_nodes_and_queue(miniport->unbound_t.incoming_data); AbortOnCondition(queueFreeSuccess == -1, "Queue_free failed in miniport_destroy()");

		//free semaphore
		semaphore_destroy(miniport->unbound_t.datagrams_ready);
	}
	else //if bounded port
	{
		int portNum = miniport->port_number;
		g_boundedPortAvail[portNum - BOUNDED_PORT_START] = true; //set the avail of our bounded port to true
	}

	free(miniport);
}

int
minimsg_send(miniport_t* local_unbound_port, miniport_t* local_bound_port, minimsg_t* msg, int len)
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
	pack_unsigned_short(header.source_port, local_unbound_port->port_number);
	pack_address(header.destination_address, local_bound_port->bound_t.remote_addr);
	pack_unsigned_short(header.destination_port, local_bound_port->bound_t.remote_unbound_port);
	
	//send message now
	int sentBytes = network_send_pkt(local_bound_port->bound_t.remote_addr, sizeof(header), (char*)&header, len, msg);

	if (sentBytes == -1) return -1; //we failed to send our message
	else return sentBytes - sizeof(header); //else return size of our message not inclusive of header
}

int
minimsg_receive(miniport_t* local_unbound_port, miniport_t** new_local_bound_port, minimsg_t* msg, int *len)
{
	assert(g_boundPortCounter >= 0); //sanity check to ensure minimsg_initialize() has been called first

	//validate input
	if (local_unbound_port == NULL|| msg == NULL || len == NULL) return -1;

	assert(local_unbound_port->port_type == 'u' && local_unbound_port->unbound_t.datagrams_ready != NULL && local_unbound_port->unbound_t.datagrams_ready != NULL);

	semaphore_P(local_unbound_port->unbound_t.datagrams_ready); //P the semaphore, if the count is 0 we're blocked until packet arrives

	//once a packet arrives and we've woken
	network_interrupt_arg_t* dequeuedPacket = NULL;
	assert(queue_length(local_unbound_port->unbound_t.incoming_data) != 0); //sanity check - our queue should have a packet waiting
	int dequeueSuccess = queue_dequeue(local_unbound_port->unbound_t.incoming_data, (void**)&dequeuedPacket); AbortOnCondition(dequeueSuccess != 0, "Queue_dequeue failed in minimsg_receive()");

	//validate our packet
	if (dequeuedPacket->buffer != NULL || dequeuedPacket->size < 0 || dequeuedPacket->sender == NULL) return -1;

	//unpack our packet
	network_address_t sender_addr;
	unpack_address((char*)sender_addr, dequeuedPacket->sender);
	int packetSize = dequeuedPacket->size;

	//create our new local bound port pointed back to the sender
	*new_local_bound_port = miniport_create_bound(sender_addr, local_unbound_port->port_number); 
	if (*new_local_bound_port == NULL) return -1;

	//get our header and message from the dequeued packet
	mini_header_t *receivedHeader;
	memcpy(receivedHeader, dequeuedPacket->buffer, sizeof(mini_header_t));
	memcpy(msg, dequeuedPacket->buffer + sizeof(mini_header_t), packetSize - sizeof(mini_header_t));
	
	//if the length of the message received was longer than *len, return *len. Otherwise, set *len to the length of our received message
	if (packetSize - sizeof(mini_header_t) <= *len) *len = packetSize - sizeof(mini_header_t);

    return *len; //return data payload bytes received not inclusive of header
}
