Kevin Zhu klz22

Jacob Herrera Jeh365

Notes about optional application:
The optional application lives in application.c. It allows users to send a message by typing in a hostname:port instead 
of typing in the addr:port. When this method is called, it will try to find the host name by using  gethostbyname. 
If it cannot find a host name, it returns. Otherwise, it will get the addr of the host name and parse the message
automatically so it is in the addr:port format for sending messages and then it will pass the message along
to our send_received to send it normally.

