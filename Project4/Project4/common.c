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

 // Forward declaration of functions defined elsewhere
void minimsg_network_handler(network_interrupt_arg_t* arg);
void minisocket_network_handler(network_interrupt_arg_t* arg);

void
common_network_handler(network_interrupt_arg_t* arg)
{
	interrupt_level_t old_level = set_interrupt_level(DISABLED); //disable interrupt

	//if packet size is less than header size, don't enqueue it and just return. mini_header_t is smaller than mini_header_reliable_t
	if (arg->size < sizeof(mini_header_t))
	{
		free(arg);
		set_interrupt_level(old_level); //restore interrupt level
		return;
	}

	//Get header and destination port
	mini_header_t *receivedHeaderPtr = arg->buffer;
	AbortOnCondition(receivedHeaderPtr->protocol != PROTOCOL_MINIDATAGRAM || receivedHeaderPtr->protocol != PROTOCOL_MINISTREAM, "Invalid protocols.");

	//check if it is a UDP or TCP packet
	if (receivedHeaderPtr->protocol == PROTOCOL_MINIDATAGRAM) //if UDP packet
	{
		minimsg_network_handler(arg);
	}
	else //if TCP packet
	{
		if (arg->size < sizeof(mini_header_reliable_t)) //if size is less than TCP header, invalid packet
		{
			free(arg);
			set_interrupt_level(old_level); //restore interrupt level
			return;
		}
		else minisocket_network_handler(arg);
	}

	set_interrupt_level(old_level); //restore interrupt level
}