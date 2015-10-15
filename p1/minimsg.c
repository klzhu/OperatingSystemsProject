/*
 *  Implementation of minimsgs and miniports.
 */
#include "minimsg.h"
#include "queue.h"
#include "synch.h"
#include "defs.h"
#include "interrupts.h"

// ---- Global Variables ---- //
int g_boundPortCounter = 0;

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
	g_boundPortCounter = 32768; //bounded ports range from 32768 - 65535
}

miniport_t*
miniport_create_unbound(int port_number)
{
	//ensure that port_number is valid
	if (port_number < 0 || port_number > 32767) return NULL;

	miniport_t* u_miniport = malloc(sizeof(miniport_t));
	if (u_miniport == NULL) return NULL; //malloc errored

	//create necessary semaphore and queue for unbounded miniport
	semaphore_t* datagrams_ready = semaphore_create();
	queue_t* incoming_data = queue_new();
	if (datagrams_ready == NULL || incoming_data == NULL) //error creating our sema or queue
	{
		//free newly allocated space for miniport, return null
		free(u_miniport);
		return NULL;
	}

	semaphore_initialize(datagrams_ready, 0); //initialize our waiting sema

	//set our unbounded miniport
	u_miniport->port_number = port_number;
	u_miniport->port_type = 'u';
	u_miniport->unbound_t.datagrams_ready = datagrams_ready;
	u_miniport->unbound_t.incoming_data = incoming_data;

    return u_miniport;
}

miniport_t*
miniport_create_bound(network_address_t addr, int remote_unbound_port_number)
{
	//validate input, unbound port number should be between 0 - 32767
	if (addr == NULL || remote_unbound_port_number < 0 || remote_unbound_port_number > 32767) return NULL;

	miniport_t* b_miniport = malloc(sizeof(miniport_t));
	if (b_miniport == NULL) return NULL; //malloc errored

	//disable interrupts as we set the miniport number and increment global counter
	interrupt_level_t old_level = set_interrupt_level(DISABLED);
	b_miniport->port_number = g_boundPortCounter;
	g_boundPortCounter++;
	if (g_boundPortCounter > 65535) g_boundPortCounter = 32768; //reset counter if we reach end of port space
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
	//validate input
	AbortOnCondition(miniport == NULL, "Null argument miniport in miniport_destroy()");
	assert(miniport->unbound_t.datagrams_ready != NULL && miniport->unbound_t.incoming_data != NULL); //self check

	//check if unbounded port, if so, we must free our queue and sema
	if (miniport->port_type == 'u')
	{
		//free our queue
		int queueFreeSuccess = queue_free_nodes_and_queue(miniport->unbound_t.incoming_data); AbortOnCondition(queueFreeSuccess == -1, "Queue_free failed in miniport_destroy()");

		//free semaphore
		semaphore_destroy(miniport->unbound_t.datagrams_ready);
	}

	free(miniport);
}

int
minimsg_send(miniport_t* local_unbound_port, miniport_t* local_bound_port, minimsg_t* msg, int len)
{
	//validate input 
	if (local_unbound_port == NULL || local_bound_port == NULL || msg == NULL || len < 0) return -1;



	//should call network_send_pkt to send datagrams

    return 0;
}

int
minimsg_receive(miniport_t* local_unbound_port, miniport_t** new_local_bound_port, minimsg_t* msg, int *len)
{
	//validate input
	if (local_unbound_port == NULL|| msg == NULL || len == NULL) return -1;

    return 0;
}
