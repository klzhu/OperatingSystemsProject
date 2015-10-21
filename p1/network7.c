/* Network test program 7
	
	Tests the bounds of memory by creating one thread which
	creates 40,000 ports, sends a message over them, and then
	destroys the created ports.

	USAGE: ./network7 <port>
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
#define MAX_COUNT 40000


miniport_t *listen_port;
miniport_t *send_port;

char text[] = "Hello, world!\n";
int textlen = 14;

int
thread(int* arg) {
	char buffer[BUFFER_SIZE];
	int length = BUFFER_SIZE;
	int i;
	miniport_t *from;
	network_address_t my_address;
	network_get_my_address(my_address);
	
	for (i = 0; i < MAX_COUNT; i++)
	{
		listen_port = miniport_create_unbound(i);
		send_port = miniport_create_bound(my_address, i);

		minimsg_send(listen_port, send_port, text, textlen);
		minimsg_receive(listen_port, &from, buffer, &length);
		printf("%s", buffer);
		miniport_destroy(from);

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
