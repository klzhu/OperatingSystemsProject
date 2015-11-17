/*
 * Implementations for common utilities.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include "interrupts.h"
#include "defs.h"
#include "common.h"
#include "network.h"
#include "miniheader.h"
#include "minimsg.h"
#include "minisocket.h"

 // Forward declaration of functions defined elsewhere
void minimsg_network_handler(network_interrupt_arg_t* arg);
void minisocket_network_handler(network_interrupt_arg_t* arg);

void free_network_arg(void * arg) // This is used in queue_free_nodes_and_queue()
{
	free((network_interrupt_arg_t*)arg);
}

void common_network_handler(network_interrupt_arg_t* arg)
{
	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupt

	//if packet size is less than header size, don't enqueue it and just return. mini_header_t is smaller than mini_header_reliable_t
	if (arg->size < sizeof(mini_header_t))
	{
		free(arg);
		set_interrupt_level(old_level); //restore interrupt level
		return;
	}

	mini_header_t *receivedHeaderPtr = (mini_header_t *)arg->buffer;
	switch (receivedHeaderPtr->protocol) {
	case PROTOCOL_MINIDATAGRAM: //UDP
		if (arg->size - sizeof(mini_header_t) > MINIMSG_MAX_MSG_SIZE) //discard the packet
			free(arg);
		else
			minimsg_network_handler(arg);
		break;
	case PROTOCOL_MINISTREAM:	//TCP
		if (arg->size < sizeof(mini_header_reliable_t) || arg->size > MAX_NETWORK_PKT_SIZE) //discard the packet
			free(arg);
		else
			minisocket_network_handler(arg);
		break;
	default: // discard unknown packet
		free(arg);
		break;
	}

	set_interrupt_level(old_level); //restore interrupt level
}