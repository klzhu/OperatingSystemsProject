#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "global.h"

struct gossip {
	struct gossip *next;
	struct sockaddr_in src;
	long counter;
	char *latest;
};

struct gossip *g_gossips = NULL;

struct gossip* gossip_next(struct gossip* gossip) {
    return gossip->next;
}

struct sockaddr_in gossip_src(struct gossip* gossip) {
    return gossip->src;
}

char* gossip_latest(struct gossip* gossip) {
    return gossip->latest;
}

/* A gossip message has the following format:
 *
 *	G<src_addr:src_port>/counter/payload\n
 *
 * Here <src_addr:src_port>/counter uniquely identify a message from the given source.
 */
void gossip_received(struct file_info *fi, char *line){
	char *port = index(line, ':');
	if (port == 0) {
		fprintf(stderr, "do_gossip: format is G<addr>:<port>/counter/payload\n");
		return;
	}
	*port++ = 0;

	char *ctr = index(port, '/');
	if (ctr == 0) {
		fprintf(stderr, "do_gossip: no counter\n");
		return;
	}
	*ctr++ = 0;

	char *payload = index(ctr, '/');
	if (payload == 0) {
		fprintf(stderr, "do_gossip: no payload\n");
		return;
	}
	*payload++ = 0;

	/* Get the source and message identifier.
	 */
	struct sockaddr_in addr;
	if (addr_get(&addr, line, atoi(port)) < 0) {
		return;
	}
	long counter = atol(ctr);

	/* See if we already have this gossip.
	 */
	struct gossip *g;
	for (g = g_gossips; g != 0; g = g->next) {
		if (addr_cmp(g->src, addr) != 0) {
			continue;
		}
		if (g->counter >= counter) {
			printf("already know about this gossip\n");
			return;
		}
		free(g->latest);
		break;
	}

	if (g == 0) {
		g = calloc(1, sizeof(*g));
		g->src = addr;
		g->next = g_gossips;
		g_gossips = g;
	}

	/* Restore the line.
	 */
	*--port = ':';			// port points to ':'
	*(ctr - 1) = '/';		// ctr still points to the first char of counter
	*(payload - 1) = '/';	// payload still points to the first char in the payload

	/* Save the gossip.
	 */
	int len = strlen(line);
	g->latest = malloc(len + 1);
	memcpy(g->latest, line, len + 1);
	g->counter = counter;

	/* Send the gossip to all connections except the one it came in on.
	 */
	char *msg = malloc(len + 3);
	sprintf(msg, "G%s\n", g->latest);
	file_broadcast(msg, len + 2, fi);

	msg[ctr - line] = '\0'; // make msg + 1 to represet addr as a char string 
	bool modified = update_from_gossip(&msg[1], payload); // update network graph with this new gossip 
	if (modified) update_shortest_distacnes(); // call Dijkstra's algorithm to update distances

	free(msg);
}

/* Send all gossip I have to the given peer.
 */
void gossip_to_peer(struct file_info *fi){
	struct gossip *g;
	for (g = g_gossips; g != 0; g = g->next) {
		file_info_send(fi, "G", 1);
		file_info_send(fi, g->latest, strlen(g->latest));
		file_info_send(fi, "\n", 1);
	}
}
