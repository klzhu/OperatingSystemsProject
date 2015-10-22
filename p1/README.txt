Name 1: Kevin Zhu
NetID 1: klz22

Name 2: Jacob Herrera
NetID 2: jeh365

Comments (optional):
In our design, the network handler is in minimsg.c and minithreads.c has a forward declaration of it. This way, 
the minimsg_network_handler is not exposed to outside users in minimsg.h, but is accessible by minithreads.c. 
Because the network handler has to queue the packet and V the semaphore of the miniport, I felt it made more sense
to have the method in minimsg.c. In this way, minithreads.c does not need to know the struct of miniport and 
just needs to call the minimsg_network_handler method.

We have included our own tests:
Network7 - This tests the creation of 40,000 ports, which will result in the minimsg_create_bound returning a NULL

Network8 - This tests the behavior of minimsg_receive when the received message is bigger than the buffer provided

Network9 - This tests the behavior of minimsg_send when data is longer than MINIMSG_MAX_MSG_SIZE

Network10 - This tests the behavior of creating a bounded or unbounded port given an invalid port number

myNetworkTest - This is a multipart test which allows two processes to talk back and forth. The test also
includes additional checks, such as the wrap around of creating bounded ports and having multiple threads listening on one port.
We are also able to quickly test that the wrap around fails if we create too many bounded ports, and sending a large batch 
of messages back and forth (I set this number to 1,000,000 in my testing).