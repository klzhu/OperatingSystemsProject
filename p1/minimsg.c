/*
 *  Implementation of minimsgs and miniports.
 */
#include "minimsg.h"

struct miniport
{
    int dummy; /* you should erase this field and replace it with your definition */
};

void
minimsg_initialize()
{
}

miniport_t*
miniport_create_unbound(int port_number)
{
    return 0;
}

miniport_t*
miniport_create_bound(network_address_t addr, int remote_unbound_port_number)
{
    return 0;
}

void
miniport_destroy(miniport_t* miniport)
{
}

int
minimsg_send(miniport_t* local_unbound_port, miniport_t* local_bound_port, minimsg_t* msg, int len)
{
    return 0;
}

int minimsg_receive(miniport_t* local_unbound_port, miniport_t** new_local_bound_port, minimsg_t* msg, int *len)
{
    return 0;
}
