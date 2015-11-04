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

 //   -----   Private helper functions  -----  
 // This function performs minisocket_send and takes in a msg type.
int minisocket_send_internal(minisocket_t *socket, const char *msg, int len, minisocket_error *error, char msgType);

 // ---- Constants ---- //
#define CLIENT_PORT_START		32768	/* The beginning port number for client port */
#define CLIENT_PORT_END			65535   /* The end port number for client port */
#define SERVER_PORT_START		0		/* The beginning port number for server port */
#define SERVER_PORT_END			32767	/* The end port number for server port */
#define TRANSMISSION_TRIES		7		/* Number of times we try to establish a connection */
#define MSG_SYNACK_ACK_NUM		1		/* The expected ack number to receive when we receive a MSG_SYNACK */
#define MSG_SYN_ACK_NUM			0		/* The expected ack number to receive whenw e receive a MSG_SYN */
#define MSG_ACK_INIT_ACK_NUM	1		/* The expected ack number to receive for our first MSG_ACK during our handshake*/

const int TRANSMISSION_RETRY_DELAYS[] = { 100, 200, 400, 800, 1600, 3200, 6400 }; //transmission timeouts in ms for each try

 // ---- Global Variables ---- //
int g_clientPortCounter = -1; //for incrementally assigning bounded ports
minisocket_t* g_clientPortPtrs[CLIENT_PORT_END - CLIENT_PORT_START + 1]; //tracks the pointers to all of our client ports

//g_serverPortPtrs is protected by disabling interrupt since methods accessing this is always quick
minisocket_t* g_serverPortPtrs[SERVER_PORT_END - SERVER_PORT_START + 1]; //tracks the pointers to all of our server ports

//Minisocket statuses
typedef enum { WAIT_SYN, WAIT_SYNACK, WAIT_ACK, CONNECTED, CLOSING, CLOSED } socket_state; // socket's states.

/*****	 alarm handler	 *****/
// This function wakes up the thread that the alarm was creating for by Ving the waiting sema
// arg is the minisocket retry sema to V
void minisocket_alarm_handler_function(void* arg)
{
	semaphore_V((semaphore_t*)arg);
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
	int transmissionTries; //tracks the number of times we've attempted to send our transmission
	semaphore_t *retrySema; //wait sema for thread to sleep on when we are waiting to retry transmission attempts
};

void minisocket_initialize()
{
	g_clientPortCounter = CLIENT_PORT_START; //client port range from 32768 - 65535
	memset(g_clientPortPtrs, 0, sizeof(g_clientPortPtrs)); //set array of client port pointers to null
	memset(g_serverPortPtrs, 0, sizeof(g_serverPortPtrs)); //set array of server port pointers to null
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
	c_minisocket->ack_number = 0;
	c_minisocket->seq_number = 0;
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
		while (k <= CLIENT_PORT_END - CLIENT_PORT_START && g_clientPortPtrs[k] != NULL)
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
			g_clientPortPtrs[k] = c_minisocket; //set the port to point to our pointer
		}
	}
	else //otherwise, set our port number to g_boundPortCounter
	{
		c_minisocket->port_number = g_clientPortCounter;
		g_clientPortPtrs[g_clientPortCounter - CLIENT_PORT_START] = c_minisocket; //set the avail of that port to false
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

//performs minisocket_send but also takes in the msgType so it can be called directly during handshake step
int minisocket_send_internal(minisocket_t *socket, const char *msg, int len, minisocket_error *error, char msgType)
{
	//pack the header to send
	mini_header_reliable_t header;
	header.protocol = PROTOCOL_MINISTREAM;
	header.message_type = msgType;
	pack_unsigned_int(header.seq_number, socket->seq_number);
	pack_unsigned_int(header.ack_number, socket->ack_number);
	network_address_t my_address;
	network_get_my_address(my_address);
	pack_address(header.source_address, my_address);
	pack_unsigned_short(header.source_port, socket->port_number);
	pack_address(header.destination_address, socket->remote_addr);
	pack_unsigned_short(header.destination_port, socket->remote_port_number);

	int sentBytes;
	while (socket->transmissionTries <= TRANSMISSION_TRIES) { //else succeeded, if we haven't tried to transmit 7 times yet, set up an alarm
		sentBytes = network_send_pkt(socket->remote_addr, sizeof(mini_header_reliable_t), (char*)&header, len, msg); //send the msg now
		if (sentBytes == -1) { //failed to send error
			socket->transmissionTries = 0; //reset the transmission tries
			*error = SOCKET_SENDERROR;
			return -1;
		}

		alarm_id retryAlarm = register_alarm(TRANSMISSION_RETRY_DELAYS[socket->transmissionTries], minisocket_alarm_handler_function, socket->retrySema);
		socket->transmissionTries++;
		semaphore_P(socket->retrySema);
		deregister_alarm(retryAlarm); //once we wake, dereg alarm if it hasn't gone off

		//check to see if we've received a packet. if we have, check to see if it is the ACK we're waiting for. If not, check the while loop constraints
		if (queue_length(socket->incoming_data > 0)) //we have a packet
		{
			network_interrupt_arg_t* dequeuedPacket = NULL;
			int dequeueSuccess = queue_dequeue(socket->incoming_data, (void**)&dequeuedPacket);
			if (dequeueSuccess == -1)
			{
				socket->transmissionTries = 0; //reset the transmission tries
				*error = SOCKET_RECEIVEERROR;
				return -1;
			}

			//RECHECK THE CASES, I don't think this is a comprehensive or correct yet
			mini_header_reliable_t *receivedHeaderPtr = dequeuedPacket->buffer;
			if (socket->status == WAIT_SYNACK && receivedHeaderPtr->message_type == MSG_SYNACK && receivedHeaderPtr->ack_number == MSG_SYNACK_ACK_NUM) {
				free(dequeuedPacket);
				break;
			}
			else if (socket->status == WAIT_ACK && receivedHeaderPtr->message_type == MSG_ACK) {
				free(dequeuedPacket);
				break;
			}
			else if (socket->status == CONNECTED && receivedHeaderPtr->message_type == MSG_ACK && receivedHeaderPtr->ack_number == socket->ack_number + len) {
				free(dequeuedPacket);
				break;
			}
			else if (socket->status == CLOSING && receivedHeaderPtr->message_type == MSG_ACK && receivedHeaderPtr->ack_number == socket->ack_number + 1) {
				free(dequeuedPacket);
				break;
			}
			
			free(dequeuedPacket); //if none of our above cases, free our packet and check the while loop constraints
		}
	}

	//if we exited the above loop, we've either timed out or received an ACK
	if (socket->transmissionTries > 7) { //we timed out
		socket->transmissionTries = 0;
		*error = SOCKET_NOSERVER;
		return -1;
	}

	else {
		*error = SOCKET_NOERROR;
		return sentBytes - sizeof(mini_header_reliable_t); //return the number of sent bytes excluding the header
	}
}

int minisocket_send(minisocket_t *socket, const char *msg, int len, minisocket_error *error)
{
    //validate inputs
	if (socket == NULL || msg == NULL || len < 0 || (len + sizeof(mini_header_reliable_t) > MAX_NETWORK_PKT_SIZE))
	{
		*error = SOCKET_INVALIDPARAMS;
		return -1;
	}
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
	//get the header and dest port
	mini_header_reliable_t *receivedHeaderPtr = (mini_header_reliable_t*)arg->buffer;
	int destPort = (int)unpack_unsigned_short(receivedHeaderPtr->destination_port);
	assert(receivedHeaderPtr->protocol == PROTOCOL_MINISTREAM);

	//if dest port is invalid or port has not been initialized, drop the packet
	if (destPort < SERVER_PORT_START || destPort > CLIENT_PORT_END)
	{
		free(arg);
		return;
	}

	if (destPort >= SERVER_PORT_START && destPort <= SERVER_PORT_END) //the dest port is a server port
	{
		if (g_serverPortPtrs[destPort] == NULL) { //if dest server port has not been created, drop packet and return
			free(arg);
			return;
		}
		else { //else, enqueue packet and V data ready semaphore
			assert(g_serverPortPtrs[destPort]->port_type == 's' && g_serverPortPtrs[destPort]->data_ready != NULL
				&& g_serverPortPtrs[destPort]->incoming_data != NULL);
			int appendSuccess = queue_append(g_serverPortPtrs[destPort]->incoming_data, (void*)arg); //enqueue the packet
			AbortOnCondition(appendSuccess == -1, "Queue_append failed in minimsg_network_handler()");
			semaphore_V(g_serverPortPtrs[destPort]->data_ready); //V the semaphore
		}
	}

	else { //dest port is a client port
		if (g_clientPortPtrs[destPort] == NULL) { //if dest client port hasn't been created, drop packet and return
			assert(g_clientPortPtrs[destPort]->port_type == 'c' && g_clientPortPtrs[destPort]->data_ready != NULL
				&& g_clientPortPtrs[destPort]->incoming_data != NULL);
			int appendSuccess = queue_append(g_clientPortPtrs[destPort]->incoming_data, (void*)arg); //enqueue the packet
			AbortOnCondition(appendSuccess == -1, "Queue_append failed in minimsg_network_handler()");
			semaphore_V(g_clientPortPtrs[destPort]->data_ready); //V the semaphore
		}
	}
}
