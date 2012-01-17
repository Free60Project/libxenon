#include <threads/debug_defines.h>

.globl debug_print
debug_print:
    twi 31, %r0, DEBUG_TRAP_PRINT
    blr
