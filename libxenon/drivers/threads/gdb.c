#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xenon_uart/xenon_uart.h>
#include <xenon_smc/xenon_smc.h>
#include <ppc/register.h>
#include <threads/threads.h>
#include <threads/debug.h>
#include <threads/gdb.h>

// Does exception stuff
int gdb_debug_routine(unsigned int code, PROCESSOR_DATA_BLOCK *processor)
{
    return 1;
}

// Setup the gdb stub
void gdb_init()
{
    debugRoutine = gdb_debug_routine;
}