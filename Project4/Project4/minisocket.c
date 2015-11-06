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

// This function packs our miniheader for us and returns it
void minisocket_pack_miniheader(mini_header_reliable_t* header, minisocket_t *socket, char msgType);

 // ---- Constants ---- //
#define PORT_START				0		/* The beginning port number */
#define PORT_END				65535	/* The end port number */
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
minisocket_t* g_socketPortPtrs[PORT_END - PORT_START + 1]; //tracks the pointers to all of our socket ports

//Minisocket statuses
typedef enum { WAIT_SYN, WAIT_SYNACK, WAIT_ACK, CONNECTED, SENDING, RECEIVING, CLOSING } socket_state; // socket's states.

/*****	 alarm handler	 *****/
// This function wakes up the thread that the alarm was creating for by Ving the waiting sema
// arg is our minisocket where the thread with the alarm is sending from
void minisocket_alarm_handler_function(void* arg)
{
	minisocket_t *minisocket = (minisocket_t*)arg;
	if (minisocket->waitOnAlarm == true) semaphore_V(minisocket->data_ready); //only V the semaphore if the thread is still waiting on the alarm
}

struct minisocket
{
	int port_number;
	queue_t *incoming_data;
	semaphore_t *data_ready;
	network_address_t remote_addr;
	int remote_port_number;
	unsigned int seq_number;
	unsigned int ack_number;
	socket_state status;
	int transmissionTries; //tracks the number of times we've attempted to send our transmission
	bool waitOnAlarm; //this bool indicates if there is a thread waiting on a resend alarm for this minisocket
};

void minisocket_initialize()
{
	g_clientPortCounter = CLIENT_PORT_START; //client port range from 32768 - 65535
	memset(g_socketPortPtrs, 0, sizeof(g_socketPortPtrs)); //set array of port pointers to null
}

minisocket_t* minisocket_server_create(int port, minisocket_error *error)
{
	assert(g_clientPortCounter >= 0); //sanity check to ensure minisocket_initialize() has been called first

    //validate inputs
	if (port < SERVER_PORT_START || port > SERVER_PORT_END || error == NULL)
	{
		*error = SOCKET_INVALIDPARAMS;
		return NULL;
	}

	//thread sync - mutex or disable interrupts
	if (g_socketPortPtrs[port] != NULL)
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
		s_minisocket->status = WAIT_SYN;
		s_minisocket->seq_number = 0;
		s_minisocket->ack_number = 0;
		s_minisocket->transmissionTries = 0;
		s_minisocket->waitOnAlarm = false;

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
		g_socketPortPtrs[port] = s_minisocket; //update our array of pointers for our unbounded ports

		//establish handshake
		while (s_minisocket->status != CONNECTED)
		{
			semaphore_P(s_minisocket->data_ready); //wait until we receive our first message, which should be SYN

			network_interrupt_arg_t* dequeuedPacket = NULL;
			int dequeueSuccess = queue_dequeue(s_minisocket->incoming_data, (void**)&dequeuedPacket);
			if (dequeueSuccess == -1)
			{
				semaphore_destroy(s_minisocket->data_ready); //free newly allocated space for data ready sema
				queue_free_nodes_and_queue(s_minisocket->incoming_data); //error check needed here?
				free(s_minisocket); //free our newly created minisocket
				*error = SOCKET_RECEIVEERROR;
				return NULL;
			}

			mini_header_reliable_t *receivedHeaderPtr = (mini_header_reliable_t*)dequeuedPacket->buffer;
			if (receivedHeaderPtr->message_type != MSG_SYN) { //if the message is not a SYN, drop it
				free(dequeuedPacket);
			}
			else { //we received our first SYN, send SYNACK back
				s_minisocket->status = WAIT_ACK;
				s_minisocket->ack_number++;
				network_address_copy(receivedHeaderPtr->source_address, s_minisocket->remote_addr);
				s_minisocket->remote_port_number = receivedHeaderPtr->source_port;
				int sentBytes = minisocket_send_internal(s_minisocket, NULL, 0, error, MSG_SYNACK, NULL);
				if (sentBytes != -1) //if sending the synack is unsuccessful, set status back to waiting for a SYN
				{
					s_minisocket->ack_number--;
					s_minisocket->status = WAIT_SYN;
					s_minisocket->remote_port_number = -1; //invalidate remote port number
					network_address_blankify(s_minisocket->remote_addr); //invalidate remote addr
				}
				else s_minisocket->status = CONNECTED; //else, we've been connected
			}
		}
	}

	*error = SOCKET_NOERROR;
	return g_socketPortPtrs[port];
}

minisocket_t* minisocket_client_create(const network_address_t addr, int port, minisocket_error *error)
{
	assert(g_clientPortCounter >= 0); //sanity check to ensure minisocket_initialize() has been called first

	//validate inputs
	if (port < CLIENT_PORT_START || port > CLIENT_PORT_END || addr == NULL || error == NULL)
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

	//set up our minisocket
	c_minisocket->status = WAIT_SYNACK;
	c_minisocket->ack_number = 0;
	c_minisocket->seq_number = 0;
	c_minisocket->transmissionTries = 0;
	c_minisocket->waitOnAlarm = false;
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
		int k = CLIENT_PORT_START;
		while (k <= CLIENT_PORT_END && g_socketPortPtrs[k] != NULL)
			k++; // find the first element equaling true

		if (k > CLIENT_PORT_END) { //if no port is available
			semaphore_destroy(c_minisocket->data_ready); //free newly allocated space for data ready sema
			queue_free(c_minisocket->incoming_data); 
			free(c_minisocket);	// free the newly created client socket
			*error = SOCKET_NOMOREPORTS;
			return NULL;
		}
		else { // found an available port
			c_minisocket->port_number = k; // set our port num to the available port num
			g_socketPortPtrs[k] = c_minisocket; //set the port to point to our pointer
		}
	}
	else //otherwise, set our port number to g_boundPortCounter
	{
		c_minisocket->port_number = g_clientPortCounter;
		g_socketPortPtrs[g_clientPortCounter] = c_minisocket; //set the avail of that port to false
		g_clientPortCounter++; //increment counter
	}

	//establish handshake
	while (c_minisocket->status != CONNECTED || *error != SOCKET_BUSY || *error != SOCKET_NOSERVER) //while we're not connected and haven't received a busy or no server error
	{
		int sentBytes = minisocket_send_internal(c_minisocket, NULL, 0, error, MSG_SYN, NULL); //call sender helper method to try to establish a connection
		if (sentBytes == -1) { //did not establish a connection
			semaphore_destroy(c_minisocket->data_ready); //free newly allocated space for data ready sema
			queue_free_nodes_and_queue(c_minisocket->incoming_data);
			free(c_minisocket); //free newly allocated space for miniport
			return NULL;
		}
		else c_minisocket->status = CONNECTED; //else, we received a SYNACK and are connected
	}

	*error = SOCKET_NOERROR;
	return c_minisocket;
}

void minisocket_pack_miniheader(mini_header_reliable_t *header, minisocket_t *socket, char msgType)
{
	//pack the header to send
	header->protocol = PROTOCOL_MINISTREAM;
	header->message_type = msgType;
	pack_unsigned_int(header->seq_number, socket->seq_number);
	pack_unsigned_int(header->ack_number, socket->ack_number);
	network_address_t my_address;
	network_get_my_address(my_address);
	pack_address(header->source_address, my_address);
	pack_unsigned_short(header->source_port, socket->port_number);
	pack_address(header->destination_address, socket->remote_addr);
	pack_unsigned_short(header->destination_port, socket->remote_port_number);
}

//performs minisocket_send but also takes in the msgType so it can be called directly during handshake step
int minisocket_send_internal(minisocket_t *socket, const char *msg, int len, minisocket_error *error, char msgType, int *ackNum)
{
	//pack the header to send
	mini_header_reliable_t header;
	minisocket_pack_miniheader(&header, socket, msgType);

	int sentBytes;
	while (socket->transmissionTries < TRANSMISSION_TRIES) { //if we haven't tried to transmit 7 times yet, set up an alarm
		sentBytes = network_send_pkt(socket->remote_addr, sizeof(mini_header_reliable_t), (char*)&header, len, msg); //send the msg now
		if (sentBytes == -1) { //failed to send error
			socket->transmissionTries = 0; //reset the transmission tries
			*error = SOCKET_SENDERROR;
			return -1;
		}

		socket->transmissionTries++;
		socket->waitOnAlarm = true;
		alarm_id retryAlarm = register_alarm(TRANSMISSION_RETRY_DELAYS[socket->transmissionTries], minisocket_alarm_handler_function, socket);
		semaphore_P(socket->data_ready);
		deregister_alarm(retryAlarm); //once we wake, dereg alarm if it hasn't gone off

		//check to see if we've received a packet. if we have, check to see if it is the ACK we're waiting for. If not, check the while loop constraints
		if (queue_length(socket->incoming_data) > 0) //we have a packet
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
			mini_header_reliable_t *receivedHeaderPtr = (mini_header_reliable_t*)dequeuedPacket->buffer;
			unsigned int headerAckNum = unpack_unsigned_int(receivedHeaderPtr->ack_number);
			if (socket->status == WAIT_SYNACK && receivedHeaderPtr->message_type == MSG_SYNACK && headerAckNum == MSG_SYNACK_ACK_NUM) {
				free(dequeuedPacket);
				break;
			}
			else if (socket->status == WAIT_ACK && receivedHeaderPtr->message_type == MSG_ACK) {
				free(dequeuedPacket);
				break;
			}
			else if (socket->status == CONNECTED && receivedHeaderPtr->message_type == MSG_ACK && headerAckNum == socket->ack_number + len) {
				free(dequeuedPacket);
				break;
			}
			else if (socket->status == CLOSING && receivedHeaderPtr->message_type == MSG_ACK && headerAckNum == socket->ack_number + 1) {
				free(dequeuedPacket);
				break;
			}
			else if (socket->status != CLOSING && receivedHeaderPtr->message_type == MSG_FIN) { //tried connecting to a server that is already in use 
				free(dequeuedPacket);
				*error = SOCKET_BUSY;
				return -1;
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
	if (socket == NULL || msg == NULL || len < 0 || error == NULL || socket->status != CONNECTED || socket->status != RECEIVING || socket->status != SENDING)
	{
		*error = SOCKET_INVALIDPARAMS;
		return -1;
	}

	while (socket->status == RECEIVING) semaphore_P(socket->data_ready); //if the socket is receiving, block send

	//set the status of our socket to sending
	socket->status = SENDING;

	int lenToSend = len; //length of message left to send
	int totalBytesSent = 0; //number of bytes we've sent so far
	int ackReceived = 0; //the ack num we receive from our receiver

	do {
		int sendSuccess;
		if (lenToSend + sizeof(mini_header_reliable_t) > MAX_NETWORK_PKT_SIZE) //if the len left to send is greater than the max size allowed, partition it
		{
			sendSuccess = minisocket_send_internal(socket, msg + totalBytesSent, MAX_NETWORK_PKT_SIZE - sizeof(mini_header_reliable_t), error, MSG_ACK, &ackReceived);
		}
		else
		{
			sendSuccess = minisocket_send_internal(socket, msg + totalBytesSent, lenToSend, error, MSG_ACK, &ackReceived);
		}

		if (sendSuccess == -1) {
			//error code should be set in minisocket_send_internal for us
			return -1;
		}
		else {
			totalBytesSent += ackReceived - socket->seq_number;
			lenToSend -= ackReceived - socket->seq_number;
			socket->seq_number = ackReceived; //set our seq number to the ack number we received
		}
	} while (lenToSend > 0);

	socket->status = CONNECTED;
    return totalBytesSent;
}

int minisocket_receive(minisocket_t *socket, char *msg, int max_len, minisocket_error *error)
{
	int recievedBytes = 0;
	char* msgBuffer;

	//validate inputs
	if (socket == NULL || msg == NULL || max_len < 0 || error == NULL || socket->status != CONNECTED || socket->status != RECEIVING || socket->status != SENDING)
	{
		*error = SOCKET_INVALIDPARAMS;
		return -1;
	}

	assert(socket->data_ready != NULL && socket->incoming_data != NULL);

	while (socket->status == SENDING) semaphore_P(socket->data_ready); //if the socket is sending, block receive. Must change this sema


	semaphore_P(socket->data_ready); //P semaphore, if count is 0 we're blocked until packet arrives

	//once a packet arrives and we wake up
	network_interrupt_arg_t* dequeuedPacket = NULL;

	//check if we've gotten all bytes and that there is more to get
	while (recievedBytes < max_len && queue_dequeue(socket->incoming_data, &dequeuedPacket) > 0)
	{
		int i = 0;
		msgBuffer = dequeuedPacket->buffer;
		//check if we're done or if we find the end of a packet
		while (recievedBytes < max_len && i + 21 < MAX_NETWORK_PKT_SIZE)
		{
			msg[21 + i] = (dequeuedPacket->buffer)[21 + i]; //first 20 bytes are the header, want the actual data
			recievedBytes++;
			i++;
		}
	}
	free(dequeuedPacket);
    return recievedBytes;
}

void minisocket_close(minisocket_t *socket)
{
	//validate inputs
	AbortOnCondition(socket == NULL, "Null input seen in minisocket_close()");
	socket->status = CLOSING;
	minisocket_error *error;
	minisocket_send_internal(socket, NULL, 0, error, MSG_FIN, NULL);
	
	//after minisocket_send_internal returns, we can safely free our socket
	int dequeueSuccess = queue_free_nodes_and_queue(socket->incoming_data);
	AbortOnCondition(dequeueSuccess == -1, "Dequeue failed in minisocket_close()");
	semaphore_destroy(socket->data_ready);
	free(socket);
}

void minisocket_network_handler(network_interrupt_arg_t* arg)
{
	//get the header and dest port
	mini_header_reliable_t *receivedHeaderPtr = (mini_header_reliable_t*)arg->buffer;
	int destPort = (int)unpack_unsigned_short(receivedHeaderPtr->destination_port);
	assert(receivedHeaderPtr->protocol == PROTOCOL_MINISTREAM);

	//if dest port is invalid or port has not been initialized, drop the packet
	if (destPort < PORT_START || destPort > PORT_END)
	{
		free(arg);
		return;
	}

	if (g_socketPortPtrs[destPort] == NULL) { //if dest port has not been created, drop packet and return
		free(arg);
		return;
	}
	else if (g_socketPortPtrs[destPort]->status != WAIT_SYN || g_socketPortPtrs[destPort]->status != WAIT_SYNACK && network_compare_network_addresses(receivedHeaderPtr->source_address, g_socketPortPtrs[destPort]->remote_addr) == 0 || receivedHeaderPtr->source_port != g_socketPortPtrs[destPort]->remote_port_number)
	{
		//received a message from a client that is not connected with us, send fin back and drop packet
		mini_header_reliable_t finHeader;
		minisocket_pack_miniheader(&finHeader, g_socketPortPtrs[destPort], MSG_FIN);
		network_send_pkt(receivedHeaderPtr->source_address, sizeof(mini_header_reliable_t), (char*)&finHeader, 0, NULL);
		free(arg);
		return;
	}
	else { //else, enqueue packet and V data ready semaphore
		assert(g_socketPortPtrs[destPort]->data_ready != NULL
			&& g_socketPortPtrs[destPort]->incoming_data != NULL);
		int appendSuccess = queue_append(g_socketPortPtrs[destPort]->incoming_data, (void*)arg); //enqueue the packet
		AbortOnCondition(appendSuccess == -1, "Queue_append failed in minimsg_network_handler()");

		if (arg->size > sizeof(mini_header_reliable_t)) //this is a message with data in it, must send ack back. nnly V semaphore if we're not in SENDING mode
		{
			mini_header_reliable_t ackHeader;
			minisocket_pack_miniheader(&ackHeader, g_socketPortPtrs[destPort], MSG_ACK);
			if (g_socketPortPtrs[destPort]->status != SENDING) semaphore_V(g_socketPortPtrs[destPort]->data_ready); //V the semaphore
		}
		else //this is a control message, so V semaphore and set waitOnAlarm to false
		{
			semaphore_V(g_socketPortPtrs[destPort]->data_ready); //V the semaphore
			g_socketPortPtrs[destPort]->waitOnAlarm = false;
		}
	}
}
