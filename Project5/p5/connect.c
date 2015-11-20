#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ifaddrs.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include "global.h"

/* Information about files and sockets is kept here.  An outgoing connection is one
 * requested by the local user using the 'C' command.  An incoming connection is
 * one created by a remote user.  We don't try to re-establish incoming connections
 * after they break.
 */
struct file_info {
	struct file_info *next;						// linked list management
	char *uid;									// unique connection id
	int fd;										// file descriptor
	enum file_info_type {
		FI_FREE,							// unused entry
		FI_FILE,							// file descriptor, probably stdin
		FI_SERVER,							// server socket
		FI_INCOMING,						// incoming connection
		FI_OUTGOING,						// outgoing connection
	} type;										// type of file
	enum { FI_UNKNOWN, FI_KNOWN } status;		// is the peer address known?
	struct sockaddr_in addr;					// address of peer if known
	void (*handler)(struct file_info *, int events);		// event handler`
	int events;									// POLLIN, POLLOUT
	char *input_buffer;							// stuff received
	int amount_received;						// size of input buffer
	char *output_buffer;						// stuff to send
	int amount_to_send;							// size of output buffer
	union {
		struct fi_outgoing {
			enum { FI_CONNECTING, FI_CONNECTED } status;
			double connect_time;			// time of last attempt to connect
		} fi_outgoing;
	} u;
};
struct file_info *file_info;
int nfiles;
char *uid_gen = (char *) 1;

//gossip counter
unsigned long gossipCounter = 0;

extern struct gossip *gossip;
struct node_list *networkNodes = NULL;
int *networkGraph = NULL;
int *dist = NULL;
int *prev = NULL;

struct sockaddr_in my_addr;

/* Add file info about 'fd'.
 */
struct file_info *file_info_add(enum file_info_type type, int fd,
						void (*handler)(struct file_info *, int events), int events){
	struct file_info *fi = calloc(1, sizeof(*fi));
	fi->uid = uid_gen++;
	fi->type = type;
	fi->fd = fd;
	fi->handler = handler;
	fi->events = events;
	fi->next = file_info;
	file_info = fi;
	nfiles++;
	return fi;
}

/* Destroy a file_info structure.
 */
void file_info_delete(struct file_info *fi){
	struct file_info **pfi, *fi2;

	for (pfi = &file_info; (fi2 = *pfi) != 0; pfi = &fi2->next) {
		if (fi2 == fi) {
			*pfi = fi->next;
			break;
		}
	}
	free(fi->input_buffer);
	free(fi->output_buffer);
	free(fi);
}

/* Put the given message in the output buffer.
 */
void file_info_send(struct file_info *fi, char *buf, int size){
	fi->output_buffer = realloc(fi->output_buffer, fi->amount_to_send + size);
	memcpy(&fi->output_buffer[fi->amount_to_send], buf, size);
	fi->amount_to_send += size;
}

/* Same as file_info_send, but it's done on sockaddr_in instead of file_info
 */
struct file_info* sockaddr_to_file(struct sockaddr_in dst) {
    struct file_info* fi;
    for (fi = file_info; fi != 0; fi = fi->next) {
        if (addr_cmp(dst, fi->addr) == 0) {
            return fi;
        }
    }
    printf("sockaddr not connected to host");
    return NULL;
}

/* Broadcast to all connections except fi.
 */
void file_broadcast(char *buf, int size, struct file_info *fi){
	struct file_info *fi2;
	for (fi2 = file_info; fi2 != 0; fi2 = fi2->next) {
		if (fi2->type == FI_FREE || fi2 == fi) {
			continue;
		}
		if (fi2->type == FI_OUTGOING && fi2->u.fi_outgoing.status == FI_CONNECTING) {
			continue;
		}
		if (fi2->type == FI_OUTGOING || fi2->type == FI_INCOMING) {
			file_info_send(fi2, buf, size);
		}
	}
}

/* Creates a gossip message and floods it with file_broadcast
*/
void flood_gossip()
{
	size_t capacity = 256;
	char *gossipMsg = malloc(capacity);
	char *myAddrChar = addr_to_string(my_addr);
	sprintf(gossipMsg, "G%s/%lu/", myAddrChar, gossipCounter);
	free(myAddrChar); //free the malloced mem that addr_to_string returned
	size_t dataSize = strlen(gossipMsg); //# of bytes used excluding '\0' for a char string

	//construct payload
	struct file_info *fi;
	for (fi = file_info; fi != 0; fi = fi->next) { //construct our payload and append it to gossipMsg
		if (fi->type == FI_INCOMING || (fi->type == FI_OUTGOING && fi->u.fi_outgoing.status == FI_CONNECTED)) {
			char *neighborAddrChar = addr_to_string(fi->addr);
			size_t addrLength = strlen(neighborAddrChar);
			if (capacity - dataSize < addrLength + 1) { //if we don't have room to fit neighborAddrChar (excluding '\0') and ';', resize
				capacity *= 2;
				gossipMsg = realloc(gossipMsg, capacity); // double the capacity
			}
			gossipMsg[dataSize++] = ';'; // append ';' to payload
			memcpy(gossipMsg + dataSize, neighborAddrChar, addrLength); //append neighborAddrChar excluding ending '\0'
			dataSize += addrLength;
			assert(dataSize == strlen(gossipMsg));
			free(neighborAddrChar);
		}
	}

	// add ending '\n'
	if (capacity - dataSize < 2) {
		capacity = dataSize + 2;
		gossipMsg = realloc(gossipMsg, capacity); // increase size to account for '\n' and '\0'
	}
	gossipMsg[dataSize++] = '\n';	// append '\n'
	gossipMsg[dataSize] = '\0';		// append ending '\0' without increasing dataSize
	assert(dataSize == strlen(gossipMsg));

	file_broadcast(gossipMsg, (int)dataSize, NULL);
	printf("this is my payload msg %s\n", gossipMsg); //DEBUGGING, remove later
	gossipCounter++;
	free(gossipMsg);
}

/* Creates a new network graph
*/
int *createNetworkGraph(struct node_list *ndList, char *myself, struct file_info *fileInfo, struct gossip *gs)
{
	int numNodes = nl_nsites(ndList); //get the number of nodes we have
	int k = numNodes * numNodes;
	int *graph = malloc(k * sizeof(int));
	while (k--) graph[k] = INFINITY; // Initialize all distances to INFINITY

	// my direct connection
	struct file_info *fi;
	for (fi = fileInfo; fi != 0; fi = fi->next) {
		if (fi->type == FI_INCOMING || (fi->type == FI_OUTGOING && fi->u.fi_outgoing.status == FI_CONNECTED && fi->status == FI_KNOWN)) {
			char *neighborAddr = addr_to_string(fi->addr);
			//set me and my neighbors are connected with a distance of 1 (hop)
			set_dist(ndList, graph, numNodes, myself, neighborAddr, 1);
			set_dist(ndList, graph, numNodes, neighborAddr, myself, 1);
			free(neighborAddr);
		}
	}

	//set distances for other neighbors in the graph based on gossips
	while (gs != 0) {
		char *gossipSrcAddr = addr_to_string(gossip_src(gs));
		char *msg = gossip_latest(gs);

		// get to payload
		int len = strlen(msg);
		int startIdx = 0;
		// find the location of 2nd '\'
		while (startIdx < len && msg[startIdx] != '/') startIdx++;	// get first '/'
		startIdx++;	// move to the char next to the first '/'
		while (startIdx < len && msg[startIdx] != '/') startIdx++;	// get 2nd '/'
		startIdx++;	// move to the first char in the payload
		if (startIdx >= len) continue; // do nothing if there is no payload

		//set dist btw srcAddr and every neighbor in payload to 1
		int endIdx = startIdx;
		while (startIdx < len) {
			// Note that last '\n' might tbe lost in communications 
			while (endIdx < len && msg[endIdx] != '\n' && msg[endIdx] != ';' && msg[endIdx] != '\0') // finding next '\n', ';' or '\0'
				endIdx++;

			if (endIdx - startIdx >= 9) { // if found '\n', ';' or '\0', and the string between them is long enough
				char origChar = msg[endIdx];
				msg[endIdx] = '\0'; // replace it with '\0' to end current token properly as a char string
				set_dist(ndList, graph, numNodes, gossipSrcAddr, msg + startIdx, 1);
				msg[endIdx] = origChar; // restore the modified char
			}

			startIdx = endIdx + 1;
			endIdx = startIdx;
		}

		free(gossipSrcAddr);
		gs = gossip_next(gs);
	}

	return graph;
}


/* Adds a node to a nonempty node list and updates its network graph that preserves
 * all old distances and sets all distances to and from the new node to INFINITY
 * At input, ndList and graph are of the old, and at output, they are both of the updated.
 * # of nodes is returned.
*/
int AddNodeAndUpdateNetworkGraph(struct node_list **ndList, char *node, int **graph)
{
	assert(*ndList != NULL && *graph != NULL); // input node list and graph should not be empty
	if (nl_index(*ndList, node) == -1) { // if the node is not in the nost list yet
		// if not returned yet, the node is new
		nl_add(*ndList, node);	// append node to ndList
		int numNodes; // # of nodes including the new node
		int *indexMap = nl_sort_output_indexes(*ndList, &numNodes);

		int *newGraph = malloc(numNodes * numNodes * sizeof(int));

		// copy old graph's distances to new graph's distance and 
		// set distances to/from new node to INFINITY
		int k, j;
		for (k = 0; k < numNodes; k++) {
			if (indexMap[k] == numNodes - 1) { // if k is the new node
				for (j = 0; j < numNodes; j++)
					newGraph[INDEX(k, j, numNodes)] = INFINITY;
			} else {
				for (j = 0; j < numNodes; j++) {
					if (indexMap[j] == numNodes - 1) // if j is the new node
						newGraph[INDEX(k, j, numNodes)] = INFINITY;
					else // copy distance from old graph to new graph
						newGraph[INDEX(k, j, numNodes)] = *graph[INDEX(indexMap[k], indexMap[j], numNodes)];
				}
			}
		}

		free(indexMap);
		free(*graph);
		*graph = newGraph;
	}

	return nl_nsites(*ndList);
}



/* Updates the nodes struct based on a new connection we received
*/
void updateNodesFromConn(struct sockaddr_in addr)
{
	char *newNeighborAddr = addr_to_string(addr);
	char *myAddrChar = addr_to_string(my_addr);
	int numNodes = 0;

	//if our node_list struct has not been initialized, initialize it and add ourselves to it
	if (networkNodes == NULL) {
		networkNodes = nl_create();
		if (networkNodes == NULL)
		{
			fprintf(stderr, "failure in updateNodesFromConn()\n");
			return;
		}
		
		nl_add(networkNodes, myAddrChar);		//add myself to the list of nodes
		nl_add(networkNodes, newNeighborAddr);	// add new neighbor to the list of nodes

		// create a graph
		assert(networkGraph == NULL);
		networkGraph = createNetworkGraph(networkNodes, myAddrChar, file_info, gossip);
		numNodes = nl_nsites(networkNodes);
	} else {
		assert(networkGraph != NULL);
		numNodes = AddNodeAndUpdateNetworkGraph(&networkNodes, newNeighborAddr, &networkGraph);
		set_dist(networkNodes, networkGraph, numNodes, myAddrChar, newNeighborAddr, 1);
		set_dist(networkNodes, networkGraph, numNodes, newNeighborAddr, myAddrChar, 1);
	}

	if (dist != NULL) { //free old graph and related variables
		free(dist);
		free(prev);
	}

	dist = (int*)malloc(numNodes * sizeof(int));
	prev = (int*)malloc(numNodes * sizeof(int));
	dijkstra(networkGraph, numNodes, nl_index(networkNodes, myAddrChar), dist, prev);

	free(myAddrChar);
	free(newNeighborAddr);
}

/* Updates the nodes and graph based on a new gossip we received
*/
void updateFromGossip(struct sockaddr_in srcAddr, char *payload)
{
	int numNodesAtEntry = nl_nsites(networkNodes);
	char *srcAddrChar = addr_to_string(srcAddr);
	int numNodes = AddNodeAndUpdateNetworkGraph(&networkNodes, srcAddrChar, &networkGraph); // add src to node list if it is not in it yet

	size_t len = strlen(payload);
	//add every addr form the payload to our node list. nl_add won't add it if it already exists in our node list
	int startIdx = 0;
	int endIdx = startIdx;
	while (startIdx < len) {
		// Note that last '\n' might tbe lost in communications 
		while (endIdx < len && payload[endIdx] != '\n' && payload[endIdx] != ';' && payload[endIdx] != '\0') // finding next '\n', ';' or '\0'
			endIdx++; 

		if (endIdx - startIdx >= 9) { // if the string between them is long enough
			char origChar = payload[endIdx];
			payload[endIdx] = '\0'; // replace it with '\0' to end current token properly as a char string
			numNodes = AddNodeAndUpdateNetworkGraph(&networkNodes, payload + startIdx, &networkGraph); // add node to node list if needed
			set_dist(networkNodes, networkGraph, numNodes, srcAddrChar, payload + startIdx, 1); // update dist from src to current node
			payload[endIdx] = origChar; // restore the modified char
		}

		startIdx = endIdx + 1;
		endIdx = startIdx;
	}

	if (numNodesAtEntry != numNodes) {
		free(dist);
		free(prev);
		dist = (int*)malloc(numNodes * sizeof(int));
		prev = (int*)malloc(numNodes * sizeof(int));
	}

	//recompute the network graph incase there were any new nodes added
	char *myAddrChar = addr_to_string(my_addr);
	dijkstra(networkGraph, numNodes, nl_index(networkNodes, myAddrChar), dist, prev);

	free(myAddrChar);
	free(srcAddrChar);
}


/* This is a timer handler to reconnect to a peer after a period of time elapsed.
 */
static void timer_reconnect(void *arg){
	void try_connect(struct file_info *fi);

	struct file_info *fi;
	for (fi = file_info; fi != 0; fi = fi->next) {
		if (fi->type != FI_FREE && fi->uid == arg) {
			printf("reconnecting\n");
			try_connect(fi);
			return;
		}
	}
	printf("reconnect: entry not found\n");
}

/* The user typed in a C<addr>:<port> command to connect to a peer.
 */
static void connect_command(struct file_info *fi, char *addr_port){
	void try_connect(struct file_info *fi);

	if (fi->type != FI_FILE) {
		fprintf(stderr, "unexpected connect message\n");
		return;
	}

	char *p = index(addr_port, ':');
	if (p == 0) {
		fprintf(stderr, "do_connect: format is C<addr>:<port>\n");
		return;
	}
	*p++ = 0;

	struct sockaddr_in addr;
	if (addr_get(&addr, addr_port, atoi(p)) < 1) {
		return;
	}
	*--p = ':';

	fi = file_info_add(FI_OUTGOING, -1, 0, 0);
	fi->u.fi_outgoing.status = FI_CONNECTING;

	/* Even though we're connecting, you're allowed to connect using any address to
	 * a peer (including a localhost address).  So we have to wait for the Hello
	 * message until we're certain who it is on the other side.  Until then, we keep
	 * the address we know for debugging purposes.
	 */
	fi->addr = addr;

	try_connect(fi);
}

/* We uniquely identify a socket connection by the lower of the local TCP/IP address
 * and the remote one.  Because two connections cannot share TCP/IP addresses, this
 * should work.
 */
static void get_id(int skt, struct sockaddr_in *addr){
	struct sockaddr_in this, that;
	socklen_t len;

	len = sizeof(that);
	if (getsockname(skt, (struct sockaddr *) &this, &len) < 0) {
		perror("getsockname");
		exit(1);
	}
	len = sizeof(that);
	if (getpeername(skt, (struct sockaddr *) &that, &len) < 0) {;
		perror("getpeername");
		exit(1);
	}
	*addr = addr_cmp(this, that) < 0 ? this : that;
}

/* Received a H(ello) message of the form H<addr>:<port>.  This is the first message
 * that's sent over a TCP connection (in both directions) so the peers can identify
 * one another.  The port is the server port of the endpoint rather than the
 * connection's port.
 *
 * Sometimes accidentally peers try to connect to one another at the same time.
 * This code kills one of the connections.
 */
void hello_received(struct file_info *fi, char *addr_port){
	char *p = index(addr_port, ':');
	if (p == 0) {
		fprintf(stderr, "do_hello: format is H<addr>:<port>\n");
		return;
	}
	*p++ = 0;

	struct sockaddr_in addr;
	if (addr_get(&addr, addr_port, atoi(p)) < 0) {
		return;
	}
	*--p = ':';

	printf("Got hello from %s:%d on socket %d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fi->fd);

	/* If a connection breaks and is re-established, a duplicate hello message is likely
	 * to arrive, but we can ignore it as we already know the peer.
	 */
	if (fi->status == FI_KNOWN) {
		fprintf(stderr, "Duplicate hello (ignoring)\n");
		if (addr_cmp(addr, fi->addr) != 0) {
			fprintf(stderr, "do_hello: address has changed???\n");
		}
		return;
	}

	/* It is possible to set up a connection to self.  We deal with it by ignoring the
	 * Hello message but keeping the connection established.
	 */
	if (addr_cmp(addr, my_addr) == 0) {
		fprintf(stderr, "Got hello from self??? (ignoring)\n");
		return;
	}

	/* Search the connections to see if there is already a connection to this peer.
	 */
	struct file_info *fi2;
	for (fi2 = file_info; fi2 != 0; fi2 = fi2->next) {
		if (fi2->type == FI_FREE || fi2->status != FI_KNOWN) {
			continue;
		}
		if (addr_cmp(fi2->addr, addr) != 0) {
			continue;
		}

		/* Found another connection.  We're going to keep just one.  First see if
		 * this is actually an existing connection.  It may have broken.
		 */
		if (fi2->fd == -1) {
			printf("Found a defunct connection---dropping that one\n");
			if (fi2->type == FI_OUTGOING) {
				fi->type = FI_OUTGOING;
				fi->u.fi_outgoing = fi2->u.fi_outgoing;
			}
			fi2->type = FI_FREE;
			return;
		}

		/* Of the two, we keep the "lowest one".  We identify a connection by the lowest
		 * endpoint address.
		 */
		struct sockaddr_in mine, other;
		get_id(fi->fd, &mine);
		get_id(fi2->fd, &other);
		if (addr_cmp(mine, other) < 0) {
			/* We keep mine.
			 */
			printf("duplicate connection -- keep mine\n");
			if (fi2->type == FI_OUTGOING) {
				fi->type = FI_OUTGOING;
				fi->u.fi_outgoing = fi2->u.fi_outgoing;
			}
			close(fi2->fd);
			fi2->type = FI_FREE;
		}
		else {
			printf("duplicate connection -- keep other\n");

			/* The other one wins.
			 */
			if (fi->type == FI_OUTGOING) {
				fi2->type = FI_OUTGOING;
				fi2->u.fi_outgoing = fi->u.fi_outgoing;
			}
			close(fi->fd);
			fi->type = FI_FREE;
			return;
		}
	}

	/* No other connection.  Keep this one.
	 */
	fi->addr = addr;
	fi->status = FI_KNOWN;

	//flood gossip since we got a new connection
	flood_gossip();

	//update node list with new connection
	updateNodesFromConn(addr);
}

// It converts a unsigned int to a right aligned array of len bytes (without ending '\0')
// NOTE: caller must ensurre that len should be long enough to hold val (no checking in the function).
void unsigned_int_to_string(char *buffer, unsigned int val, int len)
{
	char *lastChar = buffer + len - 1; // the last char of the buffer
	while (val > 0) {
		int digit = val % 10;
		*lastChar-- = digit + '0';
		val = val / 10;
	}

	// set all beginning chars, if there is any, to empty
	while (lastChar >= buffer) {
		*lastChar-- = ' '; // set it to empty char
	}
}

/* Receives a message and passes it on if it's not meant for me.
* If it is meant for us, print it out.
*/
void send_received(char *line)
{
	printf("this is the message received: %s\n", line);
	char *port = index(line, ':');
	if (port == 0) {
		fprintf(stderr, "in send recenve: format is S<dst_addr>:<port>/TTL/payload\n");
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

	/* Get the source and message identifier.
	 */
	struct sockaddr_in dst_addr;
	if (addr_get(&dst_addr, line, atoi(port)) < 0) {
		return;
	}

	/* Restore the line.
	*/
	*--port = ':';
	*(ttl - 1) = '/'; // ttl still points to the first char of ttl
	*--payload = '/';

	if (addr_cmp(my_addr, dst_addr) == 0) //if the message is for me, print it out
	{
		printf("Received Msg: %s", payload);
	}
	else
	{
		if (ttl_l < 1)
		{
			return; //if the ttl is less than 1 and is msg is not meant for us
		}
		else
		{
			// get index of destination node
			*(ttl - 1) = '\0';	// set destination properly end so that line is destination node (char string)
			int targetNodeIndex = nl_index(networkNodes, line); // it is index of the destination node now
			*(ttl - 1) = '/';	// restore the original line

			// get index of destination node of my node
			char* myAddrChar = addr_to_string(my_addr);
			int myAddrIndex = nl_index(networkNodes, myAddrChar);
			free(myAddrChar);

			// find the neighbor on the path of the shortest distance to destination
			while (prev[targetNodeIndex] != UNDEFINED && prev[targetNodeIndex] != myAddrIndex) {
				targetNodeIndex = prev[targetNodeIndex];
			}

			if (prev[targetNodeIndex] != myAddrIndex) { // the neighbor node is found, 
				// Now targetNodeIndex is my neighbor node in the shortest path to destination.
				char *nextNodeChar = nl_name(networkNodes, targetNodeIndex); // get this neighbor's node
				struct sockaddr_in* nextNode_addr = string_to_addr(nextNodeChar); // get this neighbor's address

				// prepare the message to send to this neighbor (with ttl decreased by 1)
				//decrement the TTL and modify ttl field to store new ttl value
				ttl_l--;
				int ttl_len = payload - ttl; // number of bytes for ttl in line (payload points to '/' and ttl points to first byte of ttl field in line
				char* tmpbuf = malloc(ttl_len);
				memcpy(tmpbuf, ttl, ttl_len); // store the original ttl bytes for future restore
				unsigned_int_to_string(ttl, ttl_l, ttl_len);

				// now line has the right message, find the neighbor's fl and send the messgae
				struct file_info *fi;
				for (fi = file_info; fi != 0; fi = fi->next) { // find this neighbor's fi
					if (addr_cmp(fi->addr, *nextNode_addr) == 0) { // fi is found
						file_info_send(fi, line, strlen(line));
						break;
					}
				}

				// restore line and release memory
				memcpy(ttl, tmpbuf, ttl_len); // restore line
				free(tmpbuf);
				free(nextNodeChar);
			}
		}
	}

}

/* A line of input (a command) is received.  Look at the first character to determine
 * the type and take it from there.
 */
static void handle_line(struct file_info *fi, char *line){
	switch (line[0]) {
	case 0:
		break;
	case 'C':
		connect_command(fi, &line[1]);
		break;
	case 'G':
		gossip_received(fi, &line[1]);
		break;
	case 'H':
		hello_received(fi, &line[1]);
		break;
	case 'S':
		send_received(&line[1]);
		break;
    case 'E':
    case 'e':
        exit(0);
        break;
	default:
		fprintf(stderr, "unknown command: '%s'\n", line);
	}
}

/* Input is available on the given connection.  Add it to the input buffer and
 * see if there are any complete lines in it.
 */
static void add_input(struct file_info *fi){
	fi->input_buffer = realloc(fi->input_buffer, fi->amount_received + 100);
	int n = read(fi->fd, &fi->input_buffer[fi->amount_received], 100);
	if (n < 0) {
		perror("read");
		exit(1);
	}
	if (n > 0) {
		fi->amount_received += n;
		for (;;) {
			char *p = memchr(fi->input_buffer, '\n', fi->amount_received);
			if (p == 0) {
				break;
			}
			*p++ = 0;
			handle_line(fi, fi->input_buffer);
			int n = fi->amount_received - (p - fi->input_buffer);
			memmove(fi->input_buffer, p, n);
			fi->amount_received = n;
			if (fi->fd == 0) {
				printf("> ");
				fflush(stdout);
			}
		}
	}
}

/* Activity on a socket: input, output, or error...
 */
static void message_handler(struct file_info *fi, int events){
	if (events & (POLLERR | POLLHUP)) {
		double time;
		int error;
		socklen_t len = sizeof(error);
		if (getsockopt(fi->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
			perror("getsockopt");
		}
		switch (error) {
		case 0:
			printf("Lost connection on socket %d\n", fi->fd);
			time = timer_now() + 1;
			break;
		default:
			printf("Error '%s' on socket %d\n", strerror(error), fi->fd);
			time = timer_now() + 5;
		}

		close(fi->fd);

		/* Start a timer to reconnect.
		 */
		if (fi->type == FI_OUTGOING) {
			timer_start(time, timer_reconnect, fi->uid);
			fi->fd = -1;
			fi->u.fi_outgoing.status = FI_CONNECTING;
		}
		else {
			fi->type = FI_FREE;
		}

		//flood gossip because we lost a neighbor
		flood_gossip();

		//update network graph with disconnection
		int numNodes = nl_nsites(networkNodes);
		char *myAddr = addr_to_string(my_addr);
		char *dstAddr = addr_to_string(fi->addr);
		set_dist(networkNodes, networkGraph, numNodes, myAddr, dstAddr, INFINITY);
		set_dist(networkNodes, networkGraph, numNodes, dstAddr, myAddr, INFINITY);
		free(myAddr);
		free(dstAddr);

		return;
	}
	if (events & POLLOUT) {
		int n = send(fi->fd, fi->output_buffer, fi->amount_to_send, 0);
		if (n < 0) {
			perror("send");
		}
		if (n > 0) {
			if (n == fi->amount_to_send) {
				fi->amount_to_send = 0;
			}
			else {
				memmove(&fi->output_buffer[n], fi->output_buffer, fi->amount_to_send - n);
				fi->amount_to_send -= n;
			}
		}
	}
	if (events & POLLIN) {
		add_input(fi);
	}
	if (events & ~(POLLIN|POLLOUT|POLLERR|POLLHUP)) {
		printf("message_handler: unknown events\n");
		fi->events = 0;
	}
}

/* Send a H(ello) message to the peer to identify ourselves.
 */
static void send_hello(struct file_info *fi){
	char buffer[64];
	sprintf(buffer, "H%s:%d\n", inet_ntoa(my_addr.sin_addr), ntohs(my_addr.sin_port));
	file_info_send(fi, buffer, strlen(buffer));
}

/* This function is invoked in response to a non-blocking socket connect() call once
 * an outgoing connection is established or fails.
 */
static void connect_handler(struct file_info *fi, int events){
	if (events & (POLLERR | POLLHUP)) {
		double time;
		int error;
		socklen_t len = sizeof(error);
		if (getsockopt(fi->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
			perror("getsockopt");
		}
		switch (error) {
		case 0:
			printf("No connection on socket %d\n", fi->fd);
			time = timer_now() + 3;
			break;
		case ETIMEDOUT:
			printf("Timeout on socket %d\n", fi->fd);
			time = fi->u.fi_outgoing.connect_time + 5;
			break;
		default:
			printf("Error '%s' on socket %d\n", strerror(error), fi->fd);
			time = timer_now() + 5;
		}

		/* Start a timer to reconnect.
		 */
		timer_start(time, timer_reconnect, fi->uid);

		close(fi->fd);
		fi->fd = -1;
		fi->u.fi_outgoing.status = FI_CONNECTING;

		return;
	}
	if (events & POLLOUT) {
		printf("Socket %d connected\n", fi->fd);
		fi->handler = message_handler;
		fi->events = POLLIN;
		fi->u.fi_outgoing.status = FI_CONNECTED;

		send_hello(fi);
		gossip_to_peer(fi);
	}
	if (events & ~(POLLOUT|POLLERR|POLLHUP)) {
		printf("message_handler: unknown events %x\n", events);
		fi->events = 0;
	}
}

/* Invoke a non-blocking connect() to try and establish a connection.
 */
void try_connect(struct file_info *fi){
	int skt = socket(AF_INET, SOCK_STREAM, 0);
	if (skt < 0) {
		perror("try_connect: socket");
		return;
	}

    /* Make the socket, skt, non-blocking.
     */
    int setNonBlockSuccess = fcntl(skt, F_SETFL, O_NONBLOCK);
    if (setNonBlockSuccess < 0) //call to socket returned error
	{
		fprintf(stderr, "Call to fcntl returned error in main with error code %d\n", errno);
		return;
	}

	if (connect(skt, (struct sockaddr *) &fi->addr, sizeof(fi->addr)) < 0) {
		printf("Connecting to %s:%d on socket %d\n", inet_ntoa(fi->addr.sin_addr), ntohs(fi->addr.sin_port), skt);
		if (errno != EINPROGRESS) {
			perror("try_connect: connect");
			close(skt);
			return;
		}

		fi->fd = skt;
		fi->events = POLLOUT;
		fi->handler = connect_handler;
		fi->u.fi_outgoing.connect_time = timer_now();
	}
	else {
		printf("Connected to %s:%d on socket %d\n", inet_ntoa(fi->addr.sin_addr), ntohs(fi->addr.sin_port), skt);
		fi->fd = skt;
		fi->events = POLLIN;
		fi->handler = message_handler;
		fi->u.fi_outgoing.connect_time = timer_now();
		fi->u.fi_outgoing.status = FI_CONNECTED;
	}
}

/* Standard input is available.
 */
static void stdin_handler(struct file_info *fi, int events){
	if (events & POLLIN) {
		add_input(fi);
	}
	if (events & ~POLLIN) {
		fprintf(stderr, "input_handler: unknown events %x\n", events);
	}
}

/* Activity on the server socket.  Typically an incoming connection.  Accept the
 * connection and create a new file_info descriptor for it.
 */
static void server_handler(struct file_info *fi, int events){
	if (events & POLLIN) {
		struct sockaddr_in addr;
		socklen_t len = sizeof(addr);
		int skt = accept(fi->fd, (struct sockaddr *) &addr, &len);
		if (skt < 0) {
			perror("accept");
		}

		/* Make the socket non-blocking.
		 */
        int setNonBlockSuccess = fcntl(skt, F_SETFL, O_NONBLOCK);
	    if (setNonBlockSuccess < 0) //call to socket returned error
		{
			fprintf(stderr, "Call to fcntl returned error in main with error code %d\n", errno);
			return;
		}

		/* Keep track of the socket.
		 */
		struct file_info *fi = file_info_add(FI_INCOMING, skt, message_handler, POLLIN);

		/* We don't know yet who's on the other side exactly until we get the H(ello)
		 * message.  But for debugging it's convenient to keep the peer's socket address.
		 */
		fi->addr = addr;

		printf("New connection from %s:%d on socket %d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), skt);

		send_hello(fi);
		gossip_to_peer(fi);
	}
	if (events & ~POLLIN) {
		fprintf(stderr, "server_handler: unknown events %x\n", events);
	}
}

/* Usage: a.out [port].  If no port is specified, a default port is used.
 */
int main(int argc, char *argv[]){
	int bind_port = argc > 1 ? atoi(argv[1]) : 0;
    
	/* Read from standard input.
	 */
	struct file_info *input = file_info_add(FI_FILE, 0, stdin_handler, POLLIN);

	/* Create a TCP socket.
	 */
	int skt = socket(AF_INET, SOCK_STREAM, 0);
	if (skt < 0) //call to socket returned error
	{
		fprintf(stderr, "Call to socket returned error in main with error code %d\n", errno);
		return 1;
	}

	/* Make the socket non-blocking.
	 */
    int setNonBlockSuccess = fcntl(skt, F_SETFL, O_NONBLOCK);
    if (setNonBlockSuccess < 0) //call to socket returned error
	{
		fprintf(stderr, "Call to fcntl returned error in main with error code %d\n", errno);
		return 1;
	}

	int optVal = 0;
	int setSockOptSuccess = setsockopt(skt, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(int));
	if (setSockOptSuccess < 0) //call to socket returned error
	{
		fprintf(stderr, "Call to setsockopt returned error in main with error code %d\n", errno);
		return 1;
	}

	/* Initialize addr in as far as possible.
	 */
    struct sockaddr_in bindingAddr;
    bindingAddr.sin_family = AF_INET;
    bindingAddr.sin_port = htons(bind_port);
    bindingAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	/* Bind the socket.
	 */
    int setBindSuccess = bind(skt, (struct sockaddr *)&bindingAddr, sizeof(bindingAddr));
    if (setBindSuccess < 0) //call to socket returned error
	{
		fprintf(stderr, "Call to bind returned error in main with error code %d\n", errno);
		return 1;
	}

	/* Keep track of the socket.
	 */
	file_info_add(FI_SERVER, skt, server_handler, POLLIN);

	/* Get my address.
	 */
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	if (getsockname(skt, (struct sockaddr *) &addr, &len) < 0) {
		perror("getsocknane");
	}

	/* Get my IP addresses.
	 */
	struct ifaddrs *addr_list, *ifa;
	if (getifaddrs(&addr_list) < 0) {
		perror("getifaddrs");
		return 1;
	}
	for (ifa = addr_list; ifa != 0; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr != 0 && ifa->ifa_addr->sa_family == AF_INET &&
										!(ifa->ifa_flags & IFF_LOOPBACK)) {
			struct sockaddr_in *si = (struct sockaddr_in *) ifa->ifa_addr;
			printf("%s: %s:%d\n", ifa->ifa_name, inet_ntoa(si->sin_addr), ntohs(addr.sin_port));
			my_addr = *si;
			my_addr.sin_port = addr.sin_port;
		}
	}
	freeifaddrs(addr_list);

	/* Pretend standard input is a peer...
	 */
	input->addr = my_addr;
	input->status = FI_KNOWN;

	/* Listen on the socket.
	 */
	if (listen(skt, 5) < 0) {
		perror("listen");
		return 1;
	}
	
	// printf("server socket = %d\n", skt);
	printf("> "); fflush(stdout);
	for (;;) {
		/* Handle expired timers first.
		 */
		int timeout = timer_check();

		/* Prepare poll.
		 */
		struct pollfd *fds = calloc(nfiles, sizeof(*fds));
		struct file_info *fi, **fi_index = calloc(nfiles, sizeof(*fi_index));
		int i;
		for (i = 0, fi = file_info; fi != 0; fi = fi->next) {
			if (fi->type != FI_FREE && fi->fd >= 0) {
				fds[i].fd = fi->fd;
				fds[i].events = fi->events;
				if (fi->amount_to_send > 0) {
					fds[i].events |= POLLOUT;
				}
				fi_index[i++] = fi;
			}
		}

		int n = i;			// n may be less than nfiles
		if (poll(fds, n, timeout) < 0) {
			perror("poll");
			return 1;
		}

		/* See if there's activity on any of the files/sockets.
		 */
		for (i = 0; i < n; i++) {
			if (fds[i].revents != 0 && fi_index[i]->type != FI_FREE) {
				(*fi_index[i]->handler)(fi_index[i], fds[i].revents);
			}
		}
		free(fds);
		free(fi_index);
	}

	return 0;
}
