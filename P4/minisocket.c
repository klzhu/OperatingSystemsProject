/*
 *	Implementation of minisockets.
 */
#include "minisocket.h"

struct minisocket
{
  int dummy; /* delete this field */
  /* put your definition of minisockets here */
};

void minisocket_initialize()
{

}

minisocket_t* minisocket_server_create(int port, minisocket_error *error)
{
    // TODO
    return NULL;
}

minisocket_t* minisocket_client_create(const network_address_t addr, int port, minisocket_error *error)
{
    // TODO
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
   // TODO
}
