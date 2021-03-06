/* Network test program 9

	Tests the behavior of minimsg_send when data is longer than MINIMSG_MAX_MSG_SIZE (4096)
	Checks return value of send, prints pass or fail statement depending on value of send.

	Expected value from minimsg_send is -1 (fails).

	USAGE: ./network9 <port>
	where <port> is the minimsg port to use
*/

#include "defs.h"
#include "minithread.h"
#include "minimsg.h"
#include "synch.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

//TEST_MSG is 4097 characters in length, 1 more than MINIMSG_MAX_MSG_SIZE
#define TEST_MSG "iFUzxzU3cCwzbWqlExFJqrJuV04n1bbUUCeRfH8V3ZNyjXgWseU5W5eWUIlg7BeZbavoyjtq4jk5z6czWl0HomQvWkboRFP5WF0qhRJz9SgajA2oRUZeR0CYNUmIZCoaDgEHAfbzNLCxGkfAQpCPh6N9TibsSCSh1E25IN4GaBWnH0Yf3MoVx7huCwcOeLaoL9yoKyHosjrQzpRY9XyqYSmv1LtJ5TNw3xC1oCKXayyA1KHMT8QyZejVsfko4FtszAZWWsqzpE5FNYDpljc6FZ56xyPOp6nCB7kCMnJeIRNNAbYFKzkFnckOyL7lvlPtPhS8qpkbwGNlQSXl0Q6VZpS2IKyqNWa9Uy24YwgV5VPk2P4LBNgg4PjCRsTumw9j1J1f6tscnPNotI7p72N4SGafbDpcr3DrTJLiuhORLbpnNZZTcRJ5ixRg3GMwxnQS15eLcGUwbe1iVlrzkWi7yDOUli0oDf1JCGffuERWVSECLWXB80phwXcBpzYBTrK4Ph8gC7RLlebHfgtkiNyQjxtSpzekWXG5QRIOINrcQs34cC0VYlnmNUt3HkxNvcANPj6PyO5ys3W3wFgky28VA2pl6Qj8c0zZsbnxA7HSIONXgmfxghyXE6qCVl0RuaB0Sz27A15wVB4uNqoODIqqSFhcRcyaxnhjBDQRWOAaVgfoNYv0EZbvDAT7rqICtlof2UB1GiF7KBpAxbN2UrHgLPbcz1axborsHnCvxk1Y81D5L4PLHzLszqO3311fXR8WHEyj8y4530Cp8AtKmxoRJLgjXQXK1xPuKUfHe0rQoZiDbDVco70SrIIqN1Ac4rkNtbJmzFp5etVkr2ZTJh2c8TpNGN32hrzFrb0t219O3q5uUGgbSDy9zZ69uqP1ENi8KNtNgwHy0G4EmATC9guT2k2w4TRqBFVb62rUcsSAeSkDBhaGGb0eKgrIrnQfNbEmjvtETuqyGX4PYobnskJEGcil1CtJ50DgAt9Z6QcXLFsL5K7HEFrD0VA1pAJ2UPvaxomnntwkojfQDMlxtQS6VBbm5jtuWbxBSy04oEcq2xJiiPKB4U2ZoPMaQm1HP23e5Yb5kwpwWTwOnXwNHQjIH4gr9rAjQvE0b9kWPVMyluBlUzf3E4Lzb0htpAAbhy44icUHNV2qWLjnMwMhKgOhpEBrIYSe4YqIn4LIGhJsqDli4TORtYYnPNL4HE1iZtXJlR0zvPQwmPCAfl6uCsYA0CqIiJP8GD34Xc8ZYG67AUqbnUYKpeuuUc0S4QO5hhVXLDyt4uCbv6RJ4am6VEaRyIoH1kpjW7I1NgvAN3y0yocwy7xN4Bl6Gp5HOJ3kAxJ56vTXhYYwbAuDNCEnyCeyukYmQJx0E7oaFplQ2SGAxMTw6fcUxoEuZm3Y2lARqc49wa0Q3XX08zACk3qZT2qEsj8fMM7B98lJ6LrO9i7xXzlMCa4otp1INhMZYeCS7hrxWm1G9917r7N4T7I7W6lTgNb7rUukgm8D6pVUZmKXZUEFvsHsQQ7V8xf9HkbuAiVNgzOJ2ZIGs4B0oiMWKWlxpmc6hxgYf8KBiZDPwhVoRWtUZJTDWvm16EL1V8P7eQgBFKjLbSjWqEp8TIa0hPxo3t3T65OvvskVtOgFEPOp25U0xPVJZVjI6SHvSFqHDYV2Axpb1l7NSB8vGJECzJ4OZGCQXiIE4Tmnk0yTMPFLNfkAWeTFjf7ahXthwZ3pG82hqEQ1oIx5cLsXy1pp4Z1zewDURfYbUx96j0rK6ymt4W0Hxa3lI1ZnMNvZM0JQTkiJWeLec6j0PK17JXwXG87gqH2GXKzSf5bznRC26scB7Vxo9WSaMk3oaJTV0ayOUFP4xjynRpoj7bDB31r9k4cLFtPAAZbmmGwOADq8hGVERpEhvPsG3zbCn3XkoxPszCxXcvEBCKtEGIb5onlgOeU6GkaKoJrVWKnkzsOacUD7heHLeSByqS7OWTgRfkZztXEPr4ue0yEXxE8pNpUfHlMIUcNzyUW77ZWBiZXPE21eIW37iLfxm7uGNIJVS3OccB6xuoTrOpfoIIcFHVcqt1krQ9BYlb3lfBHS4yTS3DPso8BaiDBhRiRnDsnXjXmoGmP3jyOt7CyUlM2et7hHwn5mU2HExJ6MK5ZZQxG4aG3frLl4rqsoM7SUsBnYLZiYGAsrYCgBcecmlcxjPcpFcqxwJIGw42ijFM2izwLL0o0cGubE8Exto5AkM4ffHVa7IlJStWFzTJ82uqxryo4p9xbP5QrZTaXepC1px8RVB1nExJOSFbVTl4uVpEXYMBJIAxG2E6M6lrI0VOnPDIG3k52EH1VyKecVMSoNrGMO3XHCxnT9OljRUr146mwheUuzhfItEyz74Azm1kHrGgygmA9VmDiqONJsitr6zMR5tfMlDTR8zW28fWrqsCVqQvMf31BajrB5NixpYPHYquCDWH0bLOxFMVl4uFlfRlobWt6D7fPhqLb5mqlJlA47HFCjeZPws118P59tlHkpcWlMocnNJcJfbE4NG7BAePc0001U2ZCjpzuWmkVzXgxlhRS8fXMrgGPv9gFx2FptEHHi2IVFu4PP0e1JxroyeuC5c8OTTOGZq5VRsgi9zJ9kaHlSOpqpGxNqSWVfH2oEt9uJMXyoNJOxXTbfWbIsIyKzhrnLqZG0sKTipXSxDsozyoDINzMS4CacbFcpljr2fal1qQwzeJLDtGG3DkeA4r1iiHvOQAWgnAeJm5GG4xRBDUenEjIXBwCaJYoV8ak4EoqJQvTT838sZWwn43fpb1uqb5rpGRMPzSXP1tUShrrjrSuArfz7wJ9axthIENw2OGhYg9ycl8YBlzFWe1J50tquhJnWyEmUYL2WJOIYVLcFj9jSfv5Vc6lpULAlM7xLqt7ujfzO3m9P5WhjROmYsie0OaAfCpyWnzgvFq8Ugxq1kvCEshOPeyMCt0xseqABkBIkr5IWFl5uiiykNHTfUpYkSaZZ1Vjt3hXOxBSLNBaUNtmYXBNtfXY5luIu8i4NqZ2qmpopsHRCMm41jbzjuRgqrxSNvqhKnZa7vBrTPlXvUFyZKc1cNyCERT7N5jMAb8yPjIR0XKSoAiXnyNj0rb0esGJBJXbKKSZZwkblL7OgChF4iAyfGIlJoxT2KV6vzyIa1LGysZlCRbEjkvpjPUZ0XtJrQpyRDtMXkVOH9beMjlbHsmFVTsbkNjiX3r12jFN3QzF0oQxbV0HrHUcWAxpGoztrMH4hS6e7LvWseHzhiTRQyszLs1RP4BYia7ywemiculrENyhtyyhMWB9ygiLtXURX4LlfUV5PDzx3Aw8S7SMM15XUAJxqrK0lczFXPBj0IDq7Hkkjz1LTwpBAse82tpQIWTO1zsbbXKSS6b69VKl8IoeJjKZtoM7tFjz64CAvJRRIKumlatYGOwDA6h1t2zsfcz9MSKUZ4E34HTciQlqUrK984fqHJagFMQfIGX9xzUNV0KayzKxJsPlL16picGi4bVL1bP67FzOFEGKoQyZ4QJh2wEAKKa7ofzWLNgk1kYGj2QJXgEtpgtARr8n4bPDKpQMrst32GaaWBC93qqy7U2CejebUEJGUlG3IFU8CGwWuXX0bsfx2sDXrxRA77zBoifcg1LZPyTP6cF3wkWTlZjmrIeKjXFXuNfZkYwQtEIkwENprMF68T1Nwzf9Pi2hS3rVjZU8hmDlcoXYBYBjxy9gaqHxpwtlCcmo4ZrelfMMBDCxrXLZsn9pGjiXQRKeWWYwZsOcaYOpvz21yvf8CNZr57TW1hR0f9FaKv8jw52yNpDOISctKHu2RpO9fxkHEn1sP6SUQkpmYeGNOcYHlxonHiNPpLzHnpkiFX4LoVc7JTq3lRJR5r8kJprJtiBUe045lA4YIrX5DBDlYJR8X7li673TqSaoi5C7GGiS0DBjKrvr1cBu0DZFEUDjSGX2j25kavbCBD7Rf9jDVOFwDAm1vl1S80yBpxpg3aLGOTr72YK4c4NUN55siu9AF2F4gBSnSU3VxPpxEMkYbFVMgOrELtPAphWnkpIGfuoe2ADK5FMZPGVGfytvDVLIfNwjJpoa4iYNBpD2abnmpBsKJ6TtFt"
#define BUFFER_SIZE 4097

miniport_t *listen_port;
miniport_t *send_port;

char text[] = TEST_MSG;
int textlen = 4097;

int
thread(int* arg) {
	char buffer[BUFFER_SIZE];
	int length = BUFFER_SIZE;
	//int send_result;
	miniport_t *from;
	network_address_t my_address;

	network_get_my_address(my_address);
	listen_port = miniport_create_unbound(0);
	send_port = miniport_create_bound(my_address, 0);

	if (minimsg_send(listen_port, send_port, text, textlen) == -1)
	{
		printf("Send_result == -1; Passed test!\n");
	}
	else
	{
		minimsg_receive(listen_port, &from, buffer, &length);
		printf("Send_result != -1; Failed test!\n");
	}
	return 0;
}

int
main(int argc, char** argv) {
	short fromport;
	fromport = atoi(argv[1]);
	network_udp_ports(fromport, fromport);
	textlen = strlen(text) + 1;
	minithread_system_initialize(thread, NULL);
	return -1;
}
