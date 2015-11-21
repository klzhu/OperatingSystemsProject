#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include "global.h"

/* Sends a to a ip address given a host name
* S message format: "S%s/%lu/%s\n" for dest host name, port, ttl_l, and payload
 */
void send_message_by_hostname(const char *hostname, char *message){
	struct hostent *host;
	struct sockaddr_in *saddr;

	int len = strlen(message); // get length of the message

	host = gethostbyname(hostname);
	if (host == NULL) //we could not find the host name
	{
		fprintf(stderr, "Could not determine the host name\n");
		return;
	}

	char *port = index(message, ':');

	if (port == 0) {
		fprintf(stderr, "in send send_message_by_hostname: format is S<hostname>:<port>/TTL/payload\n");
		free(saddr);
		return;
	}
	*port++ = 0;

	char *ttl = index(port, '/');
	if (ttl == 0) {
		fprintf(stderr, "in send receive: no ttl\n");
		return;
	}
	*ttl++ = 0;
	long ttl_l = atol(ttl);

	char *payload = index(ttl, '/');
	if (payload == 0) {
		fprintf(stderr, "in send receive: no payload\n");
		return;
	}
	*payload++ = 0;

	//if we got the host ip address, find shortest path and send it along the way
	saddr = malloc(sizeof(struct sockaddr_in));
	memset(saddr, 0, sizeof(*saddr));
	saddr->sin_family = AF_INET;
	saddr->sin_port = htons(atoi(port));
	saddr->sin_addr = * (struct in_addr *) host->h_addr_list[0];

	//set up the message with the ip and port number to send
	char *msg = malloc(len + 64); // add sufficient bytes in case host name was extremely short
	sprintf(msg, "S%ul:%s/%lu/%s\n", saddr->sin_addr, port, ttl_l, payload);

	send_received(msg); //send the message normally now with ip name and port num
	free(msg);
}