Name 1: Kevin Zhu
NetID 1: klz22

Name 2: Jacob Herrera
NetID 2: jeh365

Comments (optional):
In our design, the network handler is in minimsg.c and minisocket.c respectively to handle when we receive a UDP or TCP
packet. Routing these packets is the responsibility of common_network_handler found in common.c. 
We chose to put this in common.c and not common.h because this network handler should only be called by Minithreads.c, which has a forward declaration of it. This way, 
the common_network_handler is not exposed to outside users in minimsg.h, but is accessible by minithreads.c. 
Because the network handler has to touch queues and semaphores in the miniport and minisocket struct, I felt it made more sense for the handlers to live
in their respective .c files so that minithreads.c does not need to know these structs.
