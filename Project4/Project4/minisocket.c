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
#include "miniheader.h"
#include "alarm.h"

 // ---- Constants ---- //
#define CLIENT_PORT_START		32768	/* The beginning port number for client port */
#define CLIENT_PORT_END			65535   /* The end port number for client port */
#define SERVER_PORT_START		0		/* The beginning port number for server port */
#define SERVER_PORT_END			32767	/* The end port number for server port */
#define TRANSMISSION_TRIES		7		/* Number of times we try to establish a connection */

const int TRANSMISSION_RETRY_TIMEOUTS[] = { 100, 200, 400, 800, 1600, 3200, 6400 }; //transmission timeouts in ms for each try

 // ---- Global Variables ---- //
int g_clientPortCounter = -1; //for incrementally assigning bounded ports
bool g_clientPortAvail[CLIENT_PORT_END - CLIENT_PORT_START + 1]; //track availability of client ports once g_clientPortCounter goes above max value
semaphore_t* g_semaLock = NULL; // used as mutex to protect access to g_boundPortCounter & g_boundedPortAvail

//g_serverPortPtrs is protected by disabling interrupt since methods accessing this is always quick
minisocket_t* g_serverPortPtrs[SERVER_PORT_END - SERVER_PORT_START + 1]; //tracks the pointers to all of our server ports

//Minisocket statuses
typedef enum { WAIT_SYN, WAIT_SYNACK, WAIT_ACK, CONNECTED } socket_state; // socket's states.

/*****	 alarm handler	 *****/
void minisocket_alarm_handler_function(void* arg)
{
	
}

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
	socket_state status;
	semaphore_t *transWaitSema; //wait sema for thread to sleep on when we are waiting to retry transmission attempts
};

void minisocket_initialize()
{
	g_clientPortCounter = CLIENT_PORT_START; //client port range from 32768 - 65535
	memset(g_clientPortAvail, 1, sizeof(g_clientPortAvail)); //initialize array element to true, every port is avail when we initialize
	memset(g_serverPortPtrs, 0, sizeof(g_serverPortPtrs)); //set array of unbounded port pointers to null
	g_semaLock = semaphore_create(); AbortOnCondition(g_semaLock == NULL, "g_semaLock failed in minimsg_initialize()");
	semaphore_initialize(g_semaLock, 1); //init sema to 1 (available)
}

//use this fcn to send the following control msgs: SYN, SYNACK, and FIN. In these cases, we send no data. Return -1 if error, 0 otherwise
int minisocket_send_control_msg(minisocket_t *minisocket, char msgType)
{
	//construct the header to send
	mini_header_reliable_t controlHdr;
	controlHdr.protocol = PROTOCOL_MINISTREAM;
	controlHdr.message_type = msgType;
	pack_unsigned_int(controlHdr.seq_number, 0);
	pack_unsigned_int(controlHdr.ack_number, 0);
	network_address_t my_address;
	network_get_my_address(my_address);
	pack_address(controlHdr.source_address, my_address);
	pack_unsigned_short(controlHdr.source_port, minisocket->port_number);
	pack_address(controlHdr.destination_address, minisocket->remote_addr);
	pack_unsigned_short(controlHdr.destination_port, minisocket->remote_port_number);

	//set the seq and ack numbers
	if (msgType == MSG_SYN) {
		pack_unsigned_int(controlHdr.seq_number, 0);
		pack_unsigned_int(controlHdr.ack_number, 0);
	}
	else if (msgType == MSG_SYNACK) {
		pack_unsigned_int(controlHdr.seq_number, 0);
		pack_unsigned_int(controlHdr.ack_number, 1);
	}

	//send the control msg now
	int sendSuccess = network_send_pkt(minisocket->remote_addr, sizeof(mini_header_reliable_t), (char*)&controlHdr, 0, NULL);

	if (sendSuccess == -1) return -1; //failed to send error
	else return 0; //else succeeded
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
		s_minisocket->status = WAIT_SYN;

		//create necessary semaphores and queue
		s_minisocket->data_ready = semaphore_create();
		if (s_minisocket->data_ready == NULL) //error creating our data ready sema
		{
			free(s_minisocket); //free newly allocated space for miniport
			*error = SOCKET_OUTOFMEMORY;
			return NULL;
		}

		s_minisocket->incoming_data = queue_new();
		if (s_minisocket->incoming_data == NULL) //error creating our queue
		{
			semaphore_destroy(s_minisocket->data_ready); //free newly allocated space for data ready sema
			free(s_minisocket); //free newly allocated space for miniport
			*error = SOCKET_OUTOFMEMORY;
			return NULL;
		}

		semaphore_initialize(s_minisocket->data_ready, 0); //initialize our data ready sema
		g_serverPortPtrs[port] = s_minisocket; //update our array of pointers for our unbounded ports
		
		//establish handshake
		while (s_minisocket->status != CONNECTED)
		{
			if (s_minisocket->status == WAIT_SYN) semaphore_P(s_minisocket->data_ready); //wait until we receive our first message, which should be SYN
			else { //waiting for ACK
				//alarm?
			}
			network_interrupt_arg_t* dequeuedPacket = NULL;
			int dequeueSuccess = queue_dequeue(s_minisocket->incoming_data, (void**)&dequeuedPacket);
			if (dequeueSuccess == -1)
			{
				semaphore_destroy(s_minisocket->data_ready); //free newly allocated space for data ready sema
				int dequeueSuccess = queue_free_nodes_and_queue(s_minisocket->incoming_data); //error check needed here?
				free(s_minisocket); //free our newly created minisocket
				*error = SOCKET_RECEIVEERROR;
				return NULL;
			}

			mini_header_reliable_t *receivedHeaderPtr = dequeuedPacket->buffer;
			if (s_minisocket->status == WAIT_SYN) {
				s_minisocket->seq_number = 0;
				s_minisocket->ack_number = 1;
				network_address_copy(receivedHeaderPtr->source_address, s_minisocket->remote_addr);
				s_minisocket->remote_port_number = receivedHeaderPtr->source_port;
				int sendSuccess = minisocket_send_control_msg(s_minisocket, MSG_SYNACK);
				if (sendSuccess != -1) //if sending the synack is unsuccessful, set status back to waiting for a SYN
				{
					s_minisocket->status = WAIT_SYN;
					s_minisocket->remote_port_number = -1; //invalidate remote port number
					network_address_blankify(s_minisocket->remote_addr); //invalidate remote addr
				}
				else s_minisocket->status = WAIT_ACK; //else, wait to receive ACK
			}
			else //add alarm to include retries, here or make call to receive to fire alarm?
			{
				assert(s_minisocket->status == WAIT_ACK); //we should have a status of WAIT_ACK if we're in here
				
				//if a non matching remote addr sends us a message, ignore it and send MSG_FIN back. We don't care to get a response back
				if (network_compare_network_addresses(s_minisocket->remote_addr, receivedHeaderPtr->source_address) == 0 && s_minisocket->remote_port_number != receivedHeaderPtr->source_port)
					minisocket_send_control_msg(s_minisocket, MSG_FIN); 
				s_minisocket->status = CONNECTED;
			}
		}
	}

	*error = SOCKET_NOERROR;
	return g_serverPortPtrs[port];
}

minisocket_t* minisocket_client_create(const network_address_t addr, int port, minisocket_error *error)
{
	assert(g_clientPortCounter >= 0); //sanity check to ensure minisocket_initialize() has been called first

	//validate inputs
	if (port < CLIENT_PORT_START || port > CLIENT_PORT_END || addr == NULL)
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
	c_minisocket->status = WAIT_SYNACK;
	c_minisocket->remote_port_number = port;
	network_address_copy(addr, c_minisocket->remote_addr);

	//create necessary semaphores and queue
	c_minisocket->data_ready = semaphore_create();
	if (c_minisocket->data_ready == NULL) //error creating our data ready sema
	{
		free(c_minisocket); //free newly allocated space for miniport
		*error = SOCKET_OUTOFMEMORY;
		return NULL;
	}

	c_minisocket->incoming_data = queue_new();
	if (c_minisocket->incoming_data == NULL) //error creating our queue
	{
		semaphore_destroy(c_minisocket->data_ready); //free newly allocated space for data ready sema
		free(c_minisocket); //free newly allocated space for miniport
		*error = SOCKET_OUTOFMEMORY;
		return NULL;
	}

	semaphore_initialize(c_minisocket->data_ready, 0); //initialize our data ready sema

	if (g_clientPortCounter > CLIENT_PORT_END) //if we've reached the end of our port space, we need to search for an available port number
	{
		int k = 0;
		while (k <= CLIENT_PORT_END - CLIENT_PORT_START && g_clientPortAvail[k] == false)
			k++; // find the first element equaling true

		if (k > CLIENT_PORT_END - CLIENT_PORT_START) { //if no port is available
			semaphore_destroy(c_minisocket->data_ready); //free newly allocated space for data ready sema
			queue_free(c_minisocket->incoming_data); 
			free(c_minisocket);	// free the newly created client socket
			*error = SOCKET_NOMOREPORTS;
			return NULL;
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

	//establish handshake
	while (c_minisocket->status != CONNECTED)
	{
		int sendSuccess = minisocket_send_control_msg(c_minisocket, MSG_SYN);
		if (sendSuccess == -1) {
			semaphore_destroy(c_minisocket->data_ready); //free newly allocated space for data ready sema
			queue_free(c_minisocket->incoming_data);
			free(c_minisocket);	// free the newly created client socket
			*error = SOCKET_SENDERROR;
			return NULL;
		}
		semaphore_P(c_minisocket->data_ready); //wait until we receive a SYNACK
		network_interrupt_arg_t* dequeuedPacket = NULL;
		int dequeueSuccess = queue_dequeue(c_minisocket->incoming_data, (void**)&dequeuedPacket);
		if (dequeueSuccess == -1)
		{
			semaphore_destroy(c_minisocket->data_ready); //free newly allocated space for data ready sema
			int dequeueSuccess = queue_free_nodes_and_queue(c_minisocket->incoming_data); //error check needed here?
			free(c_minisocket); //free our newly created minisocket
			*error = SOCKET_RECEIVEERROR;
			return NULL;
		}
		c_minisocket->status = CONNECTED;
	}

	*error = SOCKET_NOERROR;
	return c_minisocket;
}

int minisocket_send(minisocket_t *socket, const char *msg, int len, minisocket_error *error)
{
    // TODO
    return -1;
}

int minisocket_receive(minisocket_t *socket, char *msg, int max_len, minisocket_error *error)
{
    return -1;
}

void minisocket_close(minisocket_t *socket)
{
	//validate inputs
	AbortOnCondition(socket == NULL, "Null input seen in minisocket_close()");
   // TODO
}

void minisocket_network_handler(network_interrupt_arg_t* arg)
{
	// CHECK for minisocket status
	//TODO
}
