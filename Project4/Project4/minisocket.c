/*
 *	Implementation of minisockets.
 */
#include<assert.h>
#include<string.h>
#include<stdbool.h>
#include <stdint.h>

#include "network.h"
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
#define PORT_START				0		/* The beginning port number */
#define PORT_END				65535	/* The end port number */
#define TRANSMISSION_TRIES		7		/* Number of times we try to establish a connection */
#define MAXSOCKET_MAX_MSG_SIZE	(MAX_NETWORK_PKT_SIZE - 32) /*maximum data size of a packet. Must be <= MAX_NETWORK_PKT_SIZE - NETWORK_HDR_SIZE */
const int TRANSMISSION_RETRY_DELAYS[] = { 100, 200, 400, 800, 1600, 3200, 6400 }; //transmission timeouts in ms for each try
const int FIN_WAIT_TIME = 15000; // waiting time in ms of a socket after responding MSG_ACK

// ---- Global Variables ---- //
int g_clientPortCounter = -1; //for incrementally assigning client ports
minisocket_t* g_socketPortPtrs[PORT_END - PORT_START + 1]; //tracks the pointers to all of our socket ports
semaphore_t* g_semaSocketArrayLock = NULL; // used as mutex to protect modification to g_socketPortPtrs

// ---- Data Types ---- //
// socket's wait states.
typedef enum {WAIT_SYN, WAIT_SYNACK, WAIT_ACK, WAIT_FIN, WAIT_NONE, 
			   GOT_SYN,  GOT_SYNACK,  GOT_ACK,  GOT_FIN} wait_state; 

// socket's connection states.
// socket initializes MSG_FIN is set to CLOSING, and the party received MSG_FIN to close is set to CLOSED
typedef enum {UNCONNECTED, CONNECTED, CLOSING, CLOSED} socket_state; 

struct minisocket
{
	network_address_t	remoteAddr; // Remote address
	mini_header_reliable_t header;	// Its mini_header part is fixed once connected, remaining part is for buffer in sending packet
	unsigned int seqNumber;	// for the header to send out a packet
	unsigned int ackNumber;	// for the header to send out a packet

	socket_state state;		// socket's status 
	wait_state waitStatus;	// does the socket is wait for a special packet, and what type of packet it is waiting for
	unsigned int waitAckNumber;	// What is the ack number of the waited packet
	unsigned int receivedAckNumber; // The received packet's ack_number (so that we can compare it with waitAckNumber)
	int numAlarmFired;		// # of tries to send a packet (only used by minisocket_send_a_packet() and alarm handler

	semaphore_t *waitSema;	// waiting for handshaking or ACK packet
	semaphore_t *canSend;	// for minisocket_send(): only one send can use a socket at a time
	semaphore_t *packetIsReady; // waiting for received data
	queue_t *incomingDataPackets;

	network_interrupt_arg_t* leftOverPacket; // a packet that is partially received
	int usedPacketBytes; // number of bytes used in the leftOverPacket packet
};

// ---- Internal Functions ---- //
// This is used to free a socket's resources
void free_socket(minisocket_t* socket)
{
	semaphore_destroy(socket->waitSema);
	semaphore_destroy(socket->canSend);
	semaphore_destroy(socket->packetIsReady);
	queue_free_nodes_and_queue(socket->incomingDataPackets);
	free(socket);
}

// it wakes up all threads put into sleep by socket's sema
void wakeup_all(minisocket_t* socket)
{
	while (semaphore_has_sleep_thread(socket->waitSema)) semaphore_V(socket->waitSema);
	while (semaphore_has_sleep_thread(socket->canSend)) semaphore_V(socket->canSend);
	while (semaphore_has_sleep_thread(socket->packetIsReady)) semaphore_V(socket->packetIsReady);
}

// it initializes common variables of a socket (in creating a client and server socket).
// It returns 0 if successful or -1 if error.
int init_socket_common_part(minisocket_t* socket, minisocket_error *error)
{
	//create semaphores and queue
	socket->waitSema = semaphore_create();
	socket->canSend = semaphore_create();
	socket->packetIsReady = semaphore_create();
	socket->incomingDataPackets = queue_new();
	if (socket->waitSema == NULL || socket->canSend == NULL || socket->packetIsReady == NULL || socket->incomingDataPackets == NULL) {
		*error = SOCKET_OUTOFMEMORY;
		return -1;
	}

	semaphore_initialize(socket->waitSema, 0); //initialize our data ready sema
	semaphore_initialize(socket->canSend, 1);
	semaphore_initialize(socket->packetIsReady, 0);

	socket->header.protocol = PROTOCOL_MINISTREAM;
	network_address_t my_address;
	network_get_my_address(my_address);
	pack_address(socket->header.source_address, my_address);

	socket->state = UNCONNECTED;

	socket->leftOverPacket = NULL;
	socket->usedPacketBytes = 0;

	return 0;
}

void minisocket_send_alarm_handler(void* arg)
{
	minisocket_t *socket = (minisocket_t*)arg;
	socket->numAlarmFired++;
	if (socket->waitStatus == WAIT_SYN || socket->waitStatus == WAIT_SYNACK || socket->waitStatus == WAIT_ACK) {
		semaphore_V(socket->waitSema); //only V the semaphore if the thread is waiting
	}
}

void minisocket_close_alarm_handler(void* arg)
{
	minisocket_t *socket = (minisocket_t*)arg;
	int sourcePort = unpack_unsigned_short(socket->header.source_port);
	semaphore_P(g_semaSocketArrayLock); //critical section to prevent others to modify global g_socketPortPtrs
	g_socketPortPtrs[sourcePort] = NULL;
	semaphore_V(g_semaSocketArrayLock);	//end of critical session

	wakeup_all(socket);
	free_socket(socket);
}

// It sends a packet reliably (by trying to send TRANSMISSION_TRIES times for ack).
// whatToWait -- The socket's waitStatus will be set to this value when the expected ack is received. 
// Return: # of msg bytes successfully sent if successful or -1 if failed in sending
int minisocket_send_a_packet(minisocket_t *socket, const mini_header_reliable_t* header, const char *msg, int len, wait_state whatToWait, minisocket_error *error)
{
	socket->numAlarmFired = 0;
	socket->receivedAckNumber = 0;
	socket->waitAckNumber += len;
	int numSendTries = 0;
	while (socket->numAlarmFired < TRANSMISSION_TRIES) {
		alarm_id retryAlarm;
		int sentBytes = 0;
		if (numSendTries == socket->numAlarmFired) { // need to another try of sending
			sentBytes = network_send_pkt(socket->remoteAddr, sizeof(mini_header_reliable_t), (char*)header, len, msg);
			if (sentBytes == -1) { //failed to send error
				*error = SOCKET_SENDERROR;
				return -1;
			}

			retryAlarm = register_alarm(TRANSMISSION_RETRY_DELAYS[numSendTries], minisocket_send_alarm_handler, socket);
			numSendTries++;
		}
		
		semaphore_P(socket->waitSema); //wait for ACK message

		// Check what happened
		if (socket->state == CLOSING || socket->state == CLOSED) { // socket is closed
			break;
		} else if (socket->waitStatus == whatToWait && socket->receivedAckNumber == socket->waitAckNumber) { // expected ACK is recevied
			if (numSendTries > socket->numAlarmFired) // if alarm has not set off, dereg it
				deregister_alarm(retryAlarm); 

			*error = SOCKET_NOERROR;
			assert(sentBytes - sizeof(mini_header_reliable_t) == len);
			return sentBytes - sizeof(mini_header_reliable_t);
		} 
	}

	// if not returned yet, failed in sending 
	*error = SOCKET_NOSERVER;
	return -1;
}

// ---- API Functions ---- //
void minisocket_initialize()
{
	// sanity checking
	assert(sizeof(TRANSMISSION_RETRY_DELAYS) / sizeof(int) == TRANSMISSION_TRIES); 
	assert(MAXSOCKET_MAX_MSG_SIZE < MAX_NETWORK_PKT_SIZE - sizeof(mini_header_reliable_t)); 

	g_clientPortCounter = CLIENT_PORT_START; 
	memset(g_socketPortPtrs, 0, sizeof(g_socketPortPtrs)); //set array of port pointers to null
	g_semaSocketArrayLock = semaphore_create(); 
	AbortOnCondition(g_semaSocketArrayLock == NULL, "g_semaSocketArrayLock failed in minimsg_initialize()");
	semaphore_initialize(g_semaSocketArrayLock, 1); //init sema to 1 (available).
}

minisocket_t* minisocket_server_create(int port, minisocket_error *error)
{
	//validate inputs
	if (port < SERVER_PORT_START || port > SERVER_PORT_END || error == NULL)
	{
		*error = SOCKET_INVALIDPARAMS;
		return NULL;
	}

	if (g_socketPortPtrs[port] != NULL) {
		*error = SOCKET_PORTINUSE;
		return NULL;
	}

	minisocket_t* socket = malloc(sizeof(minisocket_t));
	if (socket == NULL) //malloc errored
	{
		*error = SOCKET_OUTOFMEMORY;
		return NULL;
	}

	if (init_socket_common_part(socket, error) == -1) {
		free_socket(socket);
		return NULL;
	}

	pack_unsigned_short(socket->header.source_port, (unsigned short)port);

	socket->seqNumber = 0;
	socket->ackNumber = 1;	// MSG_SYNACK packet's ack_num is 1

	socket->waitStatus = WAIT_SYN;
	socket->waitAckNumber = 0;

	semaphore_P(g_semaSocketArrayLock); // critical section to prevent others to modify global g_socketPortPtrs
	if (g_socketPortPtrs[port] != NULL) { // check again in case it is taken by another thread since last checking
		free_socket(socket);
		*error = SOCKET_PORTINUSE;
		semaphore_V(g_semaSocketArrayLock);
		return NULL;
	}
	g_socketPortPtrs[port] = socket;	//update our array of pointers for our unbounded ports
	semaphore_V(g_semaSocketArrayLock);	//end of critical session

	//establish handshake
	// assign header's partial field for sending MSG_SYNACK packet
	socket->header.message_type = MSG_SYNACK;
	pack_unsigned_int(socket->header.seq_number, socket->seqNumber);
	pack_unsigned_int(socket->header.ack_number, socket->ackNumber);
	while (true) {
		semaphore_P(socket->waitSema); //wait for SYN message

		// At this point, SYN is received and remoteAddr and the header's destination address and port should be assigned
		socket->waitStatus = WAIT_ACK;
		socket->waitAckNumber = 1;	// ack_num of the first MSG_ACK from client
		int sentBytes = minisocket_send_a_packet(socket, &socket->header, NULL, 0, GOT_ACK, error);
		if (sentBytes != -1) { // if sent successfully
			socket->state = CONNECTED;
			socket->header.message_type = MSG_ACK;
			assert(socket->seqNumber == 1 && socket->ackNumber == 1);
			*error = SOCKET_NOERROR;
			return socket;
		}

		// if not returned yet, reset and listen again
		socket->waitStatus = WAIT_SYN; 
		socket->waitAckNumber = 0;
	}

	// the following should not be reached
	assert(false); 
	return NULL;
}

minisocket_t* minisocket_client_create(const network_address_t addr, int port, minisocket_error *error)
{
	assert(g_clientPortCounter >= 0); //sanity check to ensure minisocket_initialize() has been called first

	//validate inputs: port must be a server port
	if (port < SERVER_PORT_START || port > SERVER_PORT_END || error == NULL || addr == NULL) {
		*error = SOCKET_INVALIDPARAMS;
		return NULL;
	}

	minisocket_t* socket = malloc(sizeof(minisocket_t));
	if (socket == NULL) //malloc errored
	{
		*error = SOCKET_OUTOFMEMORY;
		return NULL;
	}

	if (init_socket_common_part(socket, error) == -1) {
		free_socket(socket);
		return NULL;
	}

	network_address_copy(addr, socket->remoteAddr);
	pack_address(socket->header.destination_address, addr);
	pack_unsigned_short(socket->header.destination_port, (unsigned short)port);

	socket->seqNumber = 0;
	socket->ackNumber = 0;	// MSG_SYN packet's ack_num is 0

	socket->waitStatus = WAIT_NONE;
	socket->waitAckNumber = 0;

	int localPort = CLIENT_PORT_START;
	semaphore_P(g_semaSocketArrayLock); //critical section to prevent others to modify global g_socketPortPtrs
	if (g_clientPortCounter > CLIENT_PORT_END) //if we've reached the end of our port space, we need to search for an available port number
	{
		// find an available client port
		while (localPort <= CLIENT_PORT_END && g_socketPortPtrs[localPort] != NULL) localPort++; 
		
		if (localPort > CLIENT_PORT_END) { //if no port is available
			free_socket(socket);
			semaphore_V(g_semaSocketArrayLock);
			*error = SOCKET_NOMOREPORTS;
			return NULL;
		} 
	} else //otherwise, set our port number to g_boundPortCounter
		localPort = g_clientPortCounter++; //increment counter

	pack_unsigned_short(socket->header.source_port, (unsigned short)localPort);
	g_socketPortPtrs[localPort] = socket;	//set the port to point to our pointer
	semaphore_V(g_semaSocketArrayLock);		//end of critical session

	//establish handshake
	socket->header.message_type = MSG_SYN;
	assert(socket->seqNumber == 0 && socket->ackNumber == 0);
	pack_unsigned_int(socket->header.seq_number, socket->seqNumber);
	pack_unsigned_int(socket->header.ack_number, socket->ackNumber);

	socket->waitStatus = WAIT_SYNACK;
	socket->waitAckNumber = 1;
	int sentBytes = minisocket_send_a_packet(socket, &socket->header, NULL, 0, GOT_SYNACK, error);
	if (sentBytes != -1) { // send MSG_ACK successfully
		// send ACK packet
		socket->seqNumber++;
		socket->ackNumber++;
		assert(socket->seqNumber == 1 && socket->ackNumber == 1);
		socket->header.message_type = MSG_ACK;
		pack_unsigned_int(socket->header.seq_number, socket->seqNumber);
		pack_unsigned_int(socket->header.ack_number, socket->ackNumber);

		sentBytes = network_send_pkt(socket->remoteAddr, sizeof(mini_header_reliable_t), (char*)&socket->header, 0, NULL);
		if (sentBytes != -1) { // send MSG_ACK successfully
			socket->state = CONNECTED;
			*error = SOCKET_NOERROR;
			return socket;
		}
	}

	// if not returned, it failed in hand shaking, we clean up
	if (socket->waitStatus == GOT_FIN) *error = SOCKET_BUSY;
	else *error = SOCKET_NOSERVER;

	semaphore_P(g_semaSocketArrayLock); //critical section to prevent others to modify global g_socketPortPtrs
	g_socketPortPtrs[localPort] = NULL; 
	semaphore_V(g_semaSocketArrayLock);	//end of critical session
	free_socket(socket);
	return NULL;
}

int minisocket_send(minisocket_t *socket, const char *msg, int len, minisocket_error *error)
{
	//validate inputs (msg == NULL && len == 0 is allowed)
	if (socket == NULL || error == NULL || len < 0 || (msg == NULL && len != 0)) {
		*error = SOCKET_INVALIDPARAMS;
		return -1;
	} else if (len == 0 || socket->state != CONNECTED) {
		*error = SOCKET_NOERROR;
		return 0;
	}

	int sentBytes = 0;
	semaphore_P(socket->canSend); // allow only one send
	if (len <= MAXSOCKET_MAX_MSG_SIZE) { // can send in one packet
		socket->waitStatus = WAIT_ACK;
		sentBytes = minisocket_send_a_packet(socket, &socket->header, msg, len, GOT_ACK, error);
	} else { // need to paritition msg into multiple packets
		while (len > 0) {
			int currLen = (len > MAXSOCKET_MAX_MSG_SIZE) ? MAXSOCKET_MAX_MSG_SIZE : len;
			socket->waitStatus = WAIT_ACK;
			int currSendBytes = minisocket_send_a_packet(socket, &socket->header, msg + sentBytes, currLen, GOT_ACK, error);
			if (currSendBytes == -1) { // sending error
				break;	// stop sending remaining data
			}

			sentBytes += currSendBytes;
			len -= currSendBytes;
		}
	}
	
	semaphore_V(socket->canSend); //release socket for other send
	return sentBytes;
}

int minisocket_receive(minisocket_t *socket, char *msg, int max_len, minisocket_error *error)
{
	//validate inputs
	if (socket == NULL || error == NULL || max_len < 0 || (msg == NULL && max_len != 0)) {
		*error = SOCKET_INVALIDPARAMS;
		return -1;
	} else if (max_len == 0 || socket->state != CONNECTED) {
		*error = SOCKET_NOERROR;
		return 0;
	}

	assert(socket->packetIsReady != NULL && socket->incomingDataPackets != NULL);
	int receivedBytes = 0;
	if (socket->leftOverPacket != NULL) { // there is data left from last receive, read it and return
		receivedBytes = socket->leftOverPacket->size - socket->usedPacketBytes;
		assert(receivedBytes > 0);
		if (receivedBytes > max_len) receivedBytes = max_len;
		memcpy(msg, socket->leftOverPacket->buffer + socket->usedPacketBytes, receivedBytes);
		socket->usedPacketBytes += receivedBytes;
		if (socket->leftOverPacket->size == socket->usedPacketBytes) { // if all bytes in the buffer are received
			free(socket->leftOverPacket); // release the packet
			socket->leftOverPacket = NULL;
			socket->usedPacketBytes = 0;
		}
	} else {
		assert(socket->usedPacketBytes == 0);

		// read from socket's queue incomingDataPackets
		semaphore_P(socket->packetIsReady); //P semaphore to wait for receiving data packet

		//once a packet arrives and we wake up
		interrupt_level_t old_level = set_interrupt_level(DISABLED); // critical session (to dequeue the packet queue)
		int dequeueSuccess = queue_dequeue(socket->incomingDataPackets, (void**)&socket->leftOverPacket);
		set_interrupt_level(old_level); //end of critical session to restore interrupt level
		AbortOnCondition(dequeueSuccess != 0, "Queue_dequeue failed in minisocket_receive()");

		int totalUsedBytes = sizeof(mini_header_reliable_t);
		int dataBytes = socket->leftOverPacket->size - totalUsedBytes;
		assert(dataBytes > 0); // if no data, the packet should not be enqueued
		receivedBytes = (dataBytes > max_len) ? max_len : dataBytes;
		memcpy(msg, socket->leftOverPacket->buffer + totalUsedBytes, receivedBytes);
		totalUsedBytes += receivedBytes;

		if (socket->leftOverPacket->size > totalUsedBytes) { // if there are some bytes left
			socket->usedPacketBytes = totalUsedBytes;
		} else { // the packet is fully received
			free(socket->leftOverPacket); // release the packet
			socket->leftOverPacket = NULL;
		}
	}

	return receivedBytes;
}

void minisocket_close(minisocket_t *socket)
{
	if (socket == NULL) return;

	assert(socket->state == CONNECTED);
	socket->state = CLOSING;

	// send MSG_FIN packet
	socket->header.message_type = MSG_FIN;
	socket->waitStatus = WAIT_FIN;
	socket->waitAckNumber = socket->seqNumber + 1; // the responding packet will increase ack_num by 1
	minisocket_error error;
	minisocket_send_a_packet(socket, &socket->header, NULL, 0, GOT_FIN, &error);

	// close the socket even when the above sending has no response
	int sourcePort = unpack_unsigned_short(socket->header.source_port);
	semaphore_P(g_semaSocketArrayLock); //critical section to prevent others to modify global g_socketPortPtrs
	g_socketPortPtrs[sourcePort] = NULL;
	semaphore_V(g_semaSocketArrayLock);	//end of critical session
	
	wakeup_all(socket);
	free_socket(socket);
}


void minisocket_network_handler(network_interrupt_arg_t* arg)
{
	//Get header and destination port
	mini_header_reliable_t *receivedHeaderPtr = (mini_header_reliable_t*)arg->buffer;
	int destPort = unpack_unsigned_short(receivedHeaderPtr->destination_port);
	//if msg is not expected or the unbounded port has not been initialized, throw away the packet
	if (destPort < PORT_START || destPort > PORT_END || g_socketPortPtrs[destPort] == NULL) {
		free(arg);
		return;
	}

	int dataBytes = arg->size - sizeof(mini_header_reliable_t);
	unsigned int receivedSeqNum = unpack_unsigned_int(receivedHeaderPtr->seq_number);
	unsigned int receivedAckNum = unpack_unsigned_int(receivedHeaderPtr->ack_number);
	minisocket_t* socket = g_socketPortPtrs[destPort];

	network_address_t remoteAddr;
	unpack_address(receivedHeaderPtr->source_address, remoteAddr);

	if (receivedHeaderPtr->message_type == MSG_SYN && socket->waitStatus != WAIT_SYN) { // send back MSG_FIN message if received packet is MSG_SYN
		mini_header_reliable_t finHeader;
		memcpy(&finHeader, &socket->header, sizeof(mini_header_reliable_t));
		memcpy(finHeader.destination_address, receivedHeaderPtr->source_address, sizeof(receivedHeaderPtr->source_address));
		memcpy(finHeader.destination_port, receivedHeaderPtr->source_port, sizeof(receivedHeaderPtr->source_port));
		finHeader.message_type = MSG_FIN;
		pack_unsigned_int(finHeader.seq_number, 0);
		pack_unsigned_int(finHeader.ack_number, 1);
		network_send_pkt(remoteAddr, sizeof(mini_header_reliable_t), (char*)&finHeader, 0, NULL);
		free(arg);
		return;
	}

	//check if remote addr+port agrees with socket's if socket has remote's info
	if (socket->waitStatus != WAIT_SYN) {
		assert(sizeof(int64_t) == 8 && sizeof(short) == 2);
		int64_t* recAddr_int = (int64_t*)receivedHeaderPtr->source_address;
		short* recPort_int = (short*)receivedHeaderPtr->source_port;
		int64_t* socketAddr_int = (int64_t*)socket->header.destination_address;
		short* socketPort_int = (short*)socket->header.destination_port;
		if (*recAddr_int != *socketAddr_int || *recPort_int != *socketPort_int) { // if packet from a different addr+port, discard it
			free(arg);
			return;
		}
	}

	// ignore packet if the socket is closed or closing but message is not MSG_FIN
	if ((socket->state == CLOSED || socket->state == CLOSING) && receivedHeaderPtr->message_type != MSG_FIN) {
		free(arg);
		return;
	}

	// packet matches socket's addr+port or MSG_SYN packet socket is waiting for
	bool matchWait = false;
	switch (receivedHeaderPtr->message_type) {
	case MSG_SYN:
		if (socket->waitStatus == WAIT_SYN && socket->waitAckNumber == receivedAckNum && dataBytes == 0) { // expected packet is received
			socket->waitStatus = GOT_SYN;
			socket->receivedAckNumber = receivedAckNum;
			unpack_address(receivedHeaderPtr->source_address, socket->remoteAddr);
			memcpy(socket->header.destination_address, receivedHeaderPtr->source_address, sizeof(receivedHeaderPtr->source_address));
			memcpy(socket->header.destination_port, receivedHeaderPtr->source_port, sizeof(receivedHeaderPtr->source_port));
			semaphore_V(socket->waitSema);
		}
		free(arg);
		break;

	case MSG_SYNACK: 
		if (socket->waitStatus == WAIT_SYNACK && socket->waitAckNumber == receivedAckNum && dataBytes == 0) {
			socket->waitStatus = GOT_SYNACK;
			socket->receivedAckNumber = receivedAckNum;
			semaphore_V(socket->waitSema);
		}
		free(arg);
		break;

	case MSG_ACK:
		if (socket->waitStatus == WAIT_ACK && socket->waitAckNumber == receivedAckNum) {
			matchWait = true;
			socket->seqNumber = socket->waitAckNumber; //???XXX???
			pack_unsigned_int(socket->header.seq_number, socket->seqNumber);
			socket->waitStatus = GOT_ACK;
			socket->receivedAckNumber = receivedAckNum;
			semaphore_V(socket->waitSema);
		}

		if (dataBytes > 0 && (socket->state == CONNECTED || (socket->state == UNCONNECTED && matchWait))
			&& socket->ackNumber == receivedSeqNum) { // right data packet & socket is ready to accept data
			socket->ackNumber += dataBytes; 
			pack_unsigned_int(socket->header.ack_number, socket->ackNumber);
			network_send_pkt(remoteAddr, sizeof(mini_header_reliable_t), (char*)&socket->header, 0, NULL);
			queue_append(socket->incomingDataPackets, (void*)arg); // append the data packet
			semaphore_V(socket->packetIsReady);
		} else { 
			free(arg);
		}
		break;

	case MSG_FIN: // no data
		if (socket->state == CLOSING) { // this socket initializes MSG_FIN 
			if (socket->waitStatus == WAIT_FIN && socket->waitAckNumber == receivedAckNum) {
				socket->waitStatus = GOT_FIN;
				socket->receivedAckNumber = receivedAckNum;
				semaphore_V(socket->waitSema);
			}
		} else { // receives MSG_FIN from the remote party
			if (socket->state != CLOSED) {
				socket->state = CLOSED;
				socket->header.message_type = MSG_FIN;
				socket->ackNumber++; // ack_num is increased by 1 to respond a MSG_FIN packet with another MSG_FIN packet
				pack_unsigned_int(socket->header.ack_number, socket->ackNumber);

				register_alarm(FIN_WAIT_TIME, minisocket_close_alarm_handler, socket);
			}

			network_send_pkt(remoteAddr, sizeof(mini_header_reliable_t), (char*)&socket->header, 0, NULL);
		}

		free(arg);
		break;
	default:
		free(arg);
		break;
	}
}
