/* Network test program 8

	Tests the behavior of minimsg_recieve when data is longer than the buffer
	
	Original message is "Hello, world!\nGoodbye, world!\n"
	Buffer size is 21, length of message is 30
	Expected value is "Hello, world!\nGoodbye"

	USAGE: ./network8 <port>
	where <port> is the minimsg port to use
*/

#include "defs.h"
#include "minithread.h"
#include "minimsg.h"
#include "synch.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define BUFFER_SIZE 21


miniport_t *listen_port;
miniport_t *send_port;

char text[] = "Hello, world!\nGoodbye, world!\n";
int textlen = 30;

int
thread(int* arg) {
	char buffer[BUFFER_SIZE];
	int length = BUFFER_SIZE;
	miniport_t *from;
	network_address_t my_address;

	network_get_my_address(my_address);
	listen_port = miniport_create_unbound(0);
	send_port = miniport_create_bound(my_address, 0);

	minimsg_send(listen_port, send_port, text, textlen);
	minimsg_receive(listen_port, &from, buffer, &length);
	printf("%s\n", buffer); //newline is to enable printing when truncated before a newline character has been reached

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


