/*
 * Common utilities header file
 */
#ifndef __COMMON_H__
#define __COMMON_H__

 /* Macro to fail gracefully. If condition, fail and give error message */
#define AbortGracefully(cond,message)                      	\
    if (cond) {                                             \
        printf("Abort: %s:%d, MSG:%s\n",                  	\
               __FILE__, __LINE__, message); 				\
        exit(1);                                             \
	    }

// Add any constants, function signatures, etc. here

#endif /*__COMMON_H__*/