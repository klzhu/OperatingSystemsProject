/*
 *	Implementation of minisockets.
 */
#include<assert.h>
#include<string.h>
#include<stdbool.h>

#include "minisocket.h"
#include "queue.h"
#include "synch.h"
#include "defs.h"
#include "interrupts.h"

 // ---- Constants ---- //
#define CLIENT_PORT_START		32768	/* The beginning port number for client port */
#define CLIENT_PORT_END			65535   /* The end port number for client port */
#define SERVER_PORT_START		0		/* The beginning port number for server port */
#define SERVER_PORT_END			32767	/* The end port number for server port */
#define TRANSMISSION_TRIES		7		/* Number of times we try to establish a connection */

 // ---- Global Variables ---- //
int g_clientPortCounter = -1; //for incrementally assigning bounded ports
bool g_clientPortAvail[CLIENT_PORT_END - CLIENT_PORT_START + 1]; //track availability of client ports once g_clientPortCounter goes above max value
semaphore_t* g_semaLock = NULL; // used as mutex to protect access to g_boundPortCounter & g_boundedPortAvail

//g_serverPortPtrs is protected by disabling interrupt since methods accessing this is always quick
miniport_t* g_serverPortPtrs[SERVER_PORT_END - SERVER_PORT_START + 1]; //tracks the pointers to all of our server ports

struct minisocket
{
	char port_type; //'s' indicates listening port, 'c' indicates client port
	int port_number;
	queue_t *incoming_data;
	semaphore_t *data_ready;
	network_address_t remote_addr;
	int remote_port_number;
	unsigned int seq_number;
	unsigned int ack_number;
};

void minisocket_initialize()
{
	g_clientPortCounter = CLIENT_PORT_START; //client port range from 32768 - 65535
	memset(g_clientPortAvail, 1, sizeof(g_clientPortAvail)); //initialize array element to true, every port is avail when we initialize
	memset(g_serverPortPtrs, 0, sizeof(g_serverPortPtrs)); //set array of unbounded port pointers to null
	g_semaLock = semaphore_create(); AbortOnCondition(g_semaLock == NULL, "g_semaLock failed in minimsg_initialize()");
	semaphore_initialize(g_semaLock, 1); //init sema to 1 (available).
}

minisocket_t* minisocket_server_create(int port, minisocket_error *error)
{
	assert(g_clientPortCounter >= 0); //sanity check to ensure minisocket_initialize() has been called first

    //validate inputs
	if (port < SERVER_PORT_START || port > SERVER_PORT_END)
	{
		*error = SOCKET_INVALIDPARAMS;
		return NULL;
	}

	//thread sync - mutex or disable interrupts
	if (g_serverPortPtrs[port] != NULL)
	{
		*error = SOCKET_PORTINUSE;
		return NULL;
	}
	else { //port is not in use, must create it

		minisocket_t* s_minisocket = malloc(sizeof(minisocket_t));
		if (s_minisocket == NULL) //malloc errored
		{
			*error = SOCKET_OUTOFMEMORY;
			return NULL;
		}
		s_minisocket->port_number = port;
		s_minisocket->port_type = 's';

		//create necessary semaphore and queue for unbounded miniport
		s_minisocket->data_ready = semaphore_create();
		if (s_minisocket->data_ready == NULL) //error creating our sema
		{
			free(s_minisocket); //free newly allocated space for miniport
			*error = SOCKET_OUTOFMEMORY;
			return NULL;
		}

		s_minisocket->incoming_data = queue_new();
		if (s_minisocket->incoming_data == NULL) //error creating our queue
		{
			semaphore_destroy(s_minisocket->data_ready); //free newly allocated space for sema
			free(s_minisocket); //free newly allocated space for miniport
			*error = SOCKET_OUTOFMEMORY;
			return NULL;
		}

		semaphore_initialize(s_minisocket->data_ready, 0); //initialize our waiting sema
		g_serverPortPtrs[port] = s_minisocket; //update our array of pointers for our unbounded ports


	}

	//set_interrupt_level(old_level); //restore interrupt level as we leave critical section
	return g_serverPortPtrs[port];
}

minisocket_t* minisocket_client_create(const network_address_t addr, int port, minisocket_error *error)
{
	assert(g_clientPortCounter >= 0); //sanity check to ensure minisocket_initialize() has been called first

	//validate inputs
	if (port < CLIENT_PORT_START || port > CLIENT_PORT_END)
	{
		*error = SOCKET_INVALIDPARAMS;
		return NULL;
	}

	minisocket_t* c_minisocket = malloc(sizeof(minisocket_t));
	if (c_minisocket == NULL) //malloc errored
	{
		*error = SOCKET_OUTOFMEMORY;
		return NULL;
	}

	c_minisocket->port_type = 'c'; //set minisocket type to client
	c_minisocket->client_port.remote_unbound_port = port;
	memcpy(c_minisocket->client_port.remote_addr, addr, sizeof(network_address_t));

	semaphore_P(g_semaLock); //critical section to access global variables g_clientPortCounter & g_clientPortAvail
	if (g_clientPortCounter > CLIENT_PORT_END) //if we've reached the end of our port space, we need to search for an available port number
	{
		int k = 0;
		while (k <= CLIENT_PORT_END - CLIENT_PORT_START && g_clientPortAvail[k] == false)
			k++; // find the first element equaling true

		if (k > CLIENT_PORT_END - CLIENT_PORT_START) { //if no port is available
			free(c_minisocket);	// free the newly created client socket
			c_minisocket = NULL; // will return NULL later
			*error = SOCKET_OUTOFMEMORY;
		}
		else { // found an available port
			c_minisocket->port_number = k + CLIENT_PORT_START; // set our port num to the available port num
			g_clientPortAvail[k] = false; //set the port to be used
		}
	}
	else //otherwise, set our port number to g_boundPortCounter
	{
		c_minisocket->port_number = g_clientPortCounter;
		g_clientPortAvail[g_clientPortCounter - CLIENT_PORT_START] = false; //set the avail of that port to false
		g_clientPortCounter++; //increment counter
	}

	semaphore_V(g_semaLock); //end of critical section

	return c_minisocket;
}

int minisocket_send(minisocket_t *socket, const char *msg, int len, minisocket_error *error)
{
    // TODO
    return -1;
}

int minisocket_receive(minisocket_t *socket, char *msg, int max_len, minisocket_error *error)
{
    // TODO
    return -1;
}

void minisocket_close(minisocket_t *socket)
{
	//validate inputs
	AbortOnCondition(socket == NULL, "Null input seen in minisocket_close()");
   // TODO
}
