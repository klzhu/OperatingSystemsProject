/* My network test program 

	send messages back and forth between two processes on different computers

	USAGE: ./network6 <souceport> <destport> [<hostname>]

	sourceport = UDP port to listen on.
	destport     = UDP port to send to.

	if no hostname is supplied, will wait for a packet before sending the first
	packet; if a hostname is given, will send and then receive.
	the receive-first copy must be started first!
*/

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "defs.h"
#include "minithread.h"
#include "minimsg.h"
#include "synch.h"

// Set the following to 1 if we want to test chopped message at a receievr
#define TEST_CHOPPED_MSG 0 

#define BUFFER_SIZE 256
#define MAX_COUNT 100 

// Forward declaration
int receive_first2(int* arg);
int transmit_first2(int* arg);

char* hostname;

int
receive_first(int* arg)
{
	char buffer[BUFFER_SIZE];
	char buffer2[BUFFER_SIZE];
	int length;
	miniport_t *port;
	miniport_t *from;

	minithread_fork(receive_first2, NULL); // generate another thread to listen to the same port

	port = miniport_create_unbound(1); // test two threads listening to the same port as in receive_first2()
	AbortOnCondition(port == NULL, "Calling receive_first() failed");

	while (true) { // run this loop (listen to the port) forever 
		if (TEST_CHOPPED_MSG) 
			length = 10; // to test chopped message
		else 
			length = BUFFER_SIZE - 1; // reserve one char for inserting ending '\0' for char string
		
		assert(length + 1 <= BUFFER_SIZE);
		int opRet = minimsg_receive(port, &from, buffer, &length);
		buffer[length + 1] = '\0'; // ensure it has at least one char string ending so that we can print it out even chopped
		AbortOnCondition(opRet == -1, "Calling minimsg_receive() failed in receive_first()");
		printf("Received msg by receive_first: %s\n", buffer);

		// set response message back to the sender
		sprintf(buffer2, "receive_first got your following message: %s\n", buffer);
		length = strlen(buffer2) + 1; // including ending char for a char string
		opRet = minimsg_send(port, from, buffer2, length);
		AbortOnCondition(opRet == -1, "Calling minimsg_send() failed in receive_first()");
		miniport_destroy(from);
	}

	return 0;
}

int
receive_first2(int* arg) 
{
	char buffer[BUFFER_SIZE];
	char buffer2[BUFFER_SIZE];
	int length;
	miniport_t *port;
	miniport_t *from;

	port = miniport_create_unbound(1);	// test two threads listening to the same port as in receive_first()
 	AbortOnCondition(port == NULL, "Calling receive_first2() failed");

	while (true) { // run this loop (listen to the port) forever 
		length = BUFFER_SIZE;
		int opRet = minimsg_receive(port, &from, buffer, &length);
		AbortOnCondition(opRet == -1, "Calling minimsg_receive() failed in receive_first2()");
		printf("Received msg by receive_first2 : %s\n", buffer);

		// set response message back to the sender
		sprintf(buffer2, "receive_first2 got your following message: %s\n", buffer);
		length = strlen(buffer2) + 1;	// including ending char for a char string
		opRet = minimsg_send(port, from, buffer2, length);
		AbortOnCondition(opRet == -1, "Calling minimsg_send() failed in receive_first2()");
		miniport_destroy(from);
	}

	return 0;
}

int
transmit_first(int* arg)
{
	const int N = 65600;

	char buffer[BUFFER_SIZE];
	int length = BUFFER_SIZE;
	int i;
	network_address_t addr;
	miniport_t *port;
	miniport_t *dest;
	miniport_t *from;

	AbortOnCondition(network_translate_hostname(hostname, addr) < 0, "Could not resolve hostname, exiting.");


	// Test port number wrapped around
	printf("Test wrap around for bound ports\n");
	for (int k = 0; k < N; k++) { // test wrap around for sending port
		printf("%d-th bound port.\n", k);
		dest = miniport_create_bound(addr, 1);
		AbortOnCondition(dest == NULL, "Calling miniport_create_(un)bound() failed");
		miniport_destroy(dest); // comment this one out if we want to see it fail when wrapping around
	}

	minithread_fork(transmit_first2, NULL);

	port = miniport_create_unbound(0);
	dest = miniport_create_bound(addr, 1);
	AbortOnCondition(port == NULL || dest == NULL, "Calling miniport_create_(un)bound() failed");

	// Test sending a batch of messages
	for (i = 0; i<MAX_COUNT; i++) {
		printf("Sending packet %d from transmit_first.\n", i + 1);
		sprintf(buffer, "Count from transmit_first is %d.\n", i + 1);
		length = strlen(buffer) + 1;
		int opRet = minimsg_send(port, dest, buffer, length);
		AbortOnCondition(opRet == -1, "Calling minimsg_send() failed in transmit_first()");
	}

	// Test receiving a batch of responses from the above sending
	for (i = 0; i < MAX_COUNT; i++) {
		length = BUFFER_SIZE;
		int opRet = minimsg_receive(port, &from, buffer, &length);
		AbortOnCondition(opRet == -1, "Calling minimsg_receive() failed in transmit_first()");
		printf("Received msg by transmit_first: %s\n", buffer);
		miniport_destroy(from);
	}

	// Testing interactive messaging (one send and one receive)
	for (i = MAX_COUNT; i<2*MAX_COUNT; i++) {
		printf("Sending packet %d from transmit_first.\n", i + 1);
		sprintf(buffer, "Count from transmit_first is %d.\n", i + 1);
		length = strlen(buffer) + 1;
		int opRet = minimsg_send(port, dest, buffer, length);
		AbortOnCondition(opRet == -1, "Calling minimsg_send() failed in transmit_first()");

		length = BUFFER_SIZE;
		opRet = minimsg_receive(port, &from, buffer, &length);
		AbortOnCondition(opRet == -1, "Calling minimsg_receive() failed in transmit_first()");
		printf("Received msg by transmit_first: %s\n", buffer);
		miniport_destroy(from);
	}

	return 0;
}

int
transmit_first2(int* arg)
{
	char buffer[BUFFER_SIZE];
	int length = BUFFER_SIZE;
	int i;
	network_address_t addr;
	miniport_t *port;
	miniport_t *dest;
	miniport_t *from;

	AbortOnCondition(network_translate_hostname(hostname, addr) < 0, "Could not resolve hostname, exiting.");

	port = miniport_create_unbound(2000);
	dest = miniport_create_bound(addr, 1);

	// Test sending a batch of messages
	for (i = 0; i<MAX_COUNT; i++) {
		printf("Sending packet %d from transmit_first2.\n", i + 1);
		sprintf(buffer, "Count from transmit_first2 is %d.\n", i + 1);
		length = strlen(buffer) + 1;
		int opRet = minimsg_send(port, dest, buffer, length);
		AbortOnCondition(opRet == -1, "Calling minimsg_send() failed in transmit_first2()");
	}

	// Test receiving a batch of responses from the above sending
	for (i = 0; i < MAX_COUNT; i++) {
		length = BUFFER_SIZE;
		int opRet = minimsg_receive(port, &from, buffer, &length);
		AbortOnCondition(opRet == -1, "Calling minimsg_receive() failed in transmit_first2()");
		printf("Received msg by transmit_first2: %s\n", buffer);
		miniport_destroy(from);
	}

	for (i = MAX_COUNT; i<2*MAX_COUNT; i++) {
		printf("Sending packet %d from transmit_first2.\n", i + 1);
		sprintf(buffer, "Count from transmit_first2 is %d.\n", i + 1);
		length = strlen(buffer) + 1;
		int opRet = minimsg_send(port, dest, buffer, length);
		AbortOnCondition(opRet == -1, "Calling minimsg_send() failed in transmit_first2()");

		length = BUFFER_SIZE;
		opRet = minimsg_receive(port, &from, buffer, &length);
		AbortOnCondition(opRet == -1, "Calling minimsg_receive() failed in transmit_first2()");
		printf("Received msg by transmit_first2: %s\n", buffer);
		miniport_destroy(from);
	}

	return 0;
}

int
main(int argc, char** argv)
{
	short fromport, toport;
	fromport = atoi(argv[1]);
	toport = atoi(argv[2]);
	network_udp_ports(fromport, toport);

	if (argc > 3) {
		hostname = argv[3];
		minithread_system_initialize(transmit_first, NULL);
	} else {
		minithread_system_initialize(receive_first, NULL);
	}

	return -1;
}
