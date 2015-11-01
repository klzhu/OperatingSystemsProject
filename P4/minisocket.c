/*
 *	Implementation of minisockets.
 */
#include "minisocket.h"
#include "queue.h"
#include "synch.h"
#include "defs.h"

 // ---- Constants ---- //
#define CLIENT_PORT_START		32768	/* The beginning port number for client port */
#define CLIENT_PORT_END			65535   /* The end port number for client port */
#define SERVER_PORT_START		0		/* The beginning port number for server port */
#define SERVER_PORT_END			32767	/* The end port number for server port */
#define CONNECTION_RETRIES		7		/* Number of times we try to establish a connection */

 // ---- Global Variables ---- //
int g_clientPortCounter = -1; //for incrementally assigning bounded ports

struct minisocket
{
	char port_type; //'s' indicates listening port, 'c' indicates client port
	int port_number;

	union {
		struct server {
			queue_t *incoming_data;
			semaphore_t *datagrams_ready;
		} server_port;
		struct client {
			network_address_t remote_addr;
			int remote_unbound_port;
		} client_port;
	};
};

void minisocket_initialize()
{
	g_clientPortCounter = CLIENT_PORT_START; //client port range from 32768 - 65535
}

minisocket_t* minisocket_server_create(int port, minisocket_error *error)
{
    //validate inputs
	if (port < SERVER_PORT_START || port > SERVER_PORT_END) return NULL;
    return NULL;
}

minisocket_t* minisocket_client_create(const network_address_t addr, int port, minisocket_error *error)
{
	//validate inputs
	if (port < CLIENT_PORT_START || port > CLIENT_PORT_END) return NULL;
    return NULL;
}

int minisocket_send(minisocket_t *socket, const char *msg, int len, minisocket_error *error)
{
    // TODO
    return -1;
}

int minisocket_receive(minisocket_t *socket, char *msg, int max_len, minisocket_error *error)
{
    // TODO
    return -1;
}

void minisocket_close(minisocket_t *socket)
{
	//validate inputs
	AbortOnCondition(socket == NULL, "Null input seen in minisocket_close()");
   // TODO
}
