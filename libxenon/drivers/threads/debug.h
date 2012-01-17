#ifndef DEBUG_H
#define	DEBUG_H

#ifdef	__cplusplus
extern "C" {
#endif
    
#include <threads/debug_defines.h>

// The debug print trap
void debug_print(const char *msg, int msg_length);

// Exception handling
// code = the exception code, pthr = the thread you are excepting on
// Return value = 1 to continue execution, 0 to crash dump
typedef unsigned int (*debug_function_proc)(unsigned int code, CONTEXT *context);
extern debug_function_proc debugRoutine; // Set this to use your own routine

// Stub routine
unsigned int debug_routine_stub(unsigned int code, CONTEXT *context);

#ifdef	__cplusplus
}
#endif

#endif	/* DEBUG_H */

