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
#define CONNECTION_RETRIES		7		/* Number of times we try to establish a connection */

 // ---- Global Variables ---- //
int g_clientPortCounter = -1; //for incrementally assigning bounded ports
bool g_clientPortAvail[CLIENT_PORT_END - CLIENT_PORT_START + 1]; //track availability of client ports once g_clientPortCounter goes above max value

//g_serverPortPtrs is protected by disabling interrupt since methods accessing this is always quick
miniport_t* g_serverPortPtrs[SERVER_PORT_END - SERVER_PORT_START + 1]; //tracks the pointers to all of our server ports

struct minisocket
{
	char port_type; //'s' indicates listening port, 'c' indicates client port
	int port_number;

	union {
		struct server {
			queue_t *incoming_data;
			semaphore_t *datagrams_ready;
		} server_port;
		struct client {
			network_address_t remote_addr;
			int remote_unbound_port;
		} client_port;
	};
};

void minisocket_initialize()
{
	g_clientPortCounter = CLIENT_PORT_START; //client port range from 32768 - 65535
	memset(g_clientPortAvail, 1, sizeof(g_clientPortAvail)); //initialize array element to true, every port is avail when we initialize
	memset(g_serverPortPtrs, 0, sizeof(g_serverPortPtrs)); //set array of unbounded port pointers to null
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
		s_minisocket->server_port.datagrams_ready = semaphore_create();
		if (s_minisocket->server_port.datagrams_ready == NULL) //error creating our sema
		{
			free(s_minisocket); //free newly allocated space for miniport
			*error = SOCKET_OUTOFMEMORY;
			return NULL;
		}

		s_minisocket->server_port.incoming_data = queue_new();
		if (s_minisocket->server_port.incoming_data == NULL) //error creating our queue
		{
			semaphore_destroy(s_minisocket->server_port.datagrams_ready); //free newly allocated space for sema
			free(s_minisocket); //free newly allocated space for miniport
			*error = SOCKET_OUTOFMEMORY;
			return NULL;
		}

		semaphore_initialize(s_minisocket->server_port.datagrams_ready, 0); //initialize our waiting sema
		g_serverPortPtrs[port] = s_minisocket; //update our array of pointers for our unbounded ports
	}

	//set_interrupt_level(old_level); //restore interrupt level as we leave critical section
	return g_serverPortPtrs[port];
}

minisocket_t* minisocket_client_create(const network_address_t addr, int port, minisocket_error *error)
{
	//validate inputs
	if (port < CLIENT_PORT_START || port > CLIENT_PORT_END)
	{
		*error = SOCKET_INVALIDPARAMS;
		return NULL;
	}

    return NULL;
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
