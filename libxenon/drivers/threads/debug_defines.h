#ifndef _DEBUG_DEFINES_H
#define _DEBUG_DEFINES_H

// Exception reasons
#define EXCEPT_REASON_TRAP 1
#define EXCEPT_REASON_PRIVILEGED_INSTRUCTION 2
#define EXCEPT_REASON_INVALID_INSTRUCTION 4
#define EXCEPT_REASON_INVALID_FLOAT_OPERATION 8

// Exception codes
// Output text to the debugger, r3 = Text, r4 = Length
#define EXCEPT_CODE_UNKNOWN                     0
#define EXCEPT_CODE_DEBUG_PRINT                 1
#define EXCEPT_CODE_TRAP                        2
#define EXCEPT_CODE_INVALID_INSTRUCTION         3
#define EXCEPT_CODE_INVALID_FLOAT_OPERATION     4
#define EXCEPT_CODE_PRIVILEGED_INSTRUCTION      5
#define EXCEPT_CODE_SEGMENTATION_FAULT_READ     6
#define EXCEPT_CODE_SEGMENTATION_FAULT_WRITE    7
#define EXCEPT_CODE_BREAKPOINT                  8 // Not used yet
    
// Debug Traps
#define DEBUG_TRAP_PRINT 0x14 // The trap for printing to the debugger

#endif