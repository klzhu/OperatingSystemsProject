/* Network test program 10

	Tests the behavior of miniport_create_XXXXXXX when given an invalid port number
	Checks return value of create, prints "passes" or "fails" depending on output.

*/

#include "defs.h"
#include "minithread.h"
#include "minimsg.h"
#include "synch.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>