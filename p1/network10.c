/* Network test program 10

	Tests the behavior of miniport_create_XXXXX when given an invalid port number
	Checks return value of create, prints "passes" or "fails" depending on output.

	Listener ports may not have numbers higher than 32767
	Sender ports may not have numbers lower than 32768

	Expected value from miniport_create_XXXXX is NULL

	USAGE: ./network10 <port>
	where <port> is the minimsg port to use
*/

#include "defs.h"
#include "minithread.h"
#include "minimsg.h"
#include "synch.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BUFFER_SIZE 256


miniport_t *listen_port;
miniport_t *send_port;

char text[] = "Hello, world!\n";
int textlen = 14;

int
thread(int* arg) {
	char buffer[BUFFER_SIZE];
	int length = BUFFER_SIZE;
	//int listen_create_result;
	//int send_create_result;
	miniport_t *from;
	network_address_t my_address;

	network_get_my_address(my_address);
	listen_port = miniport_create_unbound(32768);
	send_port = miniport_create_bound(my_address, 32767);

	if (send_port == NULL)
	{
		printf("Listener port number 32768 was not created\nPasses!\n");
	}
	else
	{
		printf("Listener port number 32768 was created\nFails!\n");
		minimsg_send(listen_port, send_port, text, textlen);
	}
	if (listen_port == NULL)
	{
		printf("Listener port number 32767 was not created\nPasses!\n");
	}
	else
	{
		printf("Listener port number 32767 was created\nFails!\n");
		minimsg_receive(listen_port, &from, buffer, &length);
	}
	return 0;
}

int
main(int argc, char** argv) {
	short fromport;
	fromport = atoi(argv[1]);
	network_udp_ports(fromport, fromport);
	textlen = strlen(text) + 1;
	minithread_system_initialize(thread, NULL);
	return -1;
}
