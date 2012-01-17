#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <console/console.h>
#include <xenon_uart/xenon_uart.h>
#include <xenon_smc/xenon_smc.h>
#include <ppc/cache.h>
#include <newlib/vfs.h>
#include <threads/threads.h>
#include <threads/debug.h>
#include <ppc/atomic.h>
#include <ppc/register.h>

static char exception_text[4096]="\0";
debug_function_proc debugRoutine = debug_routine_stub;

static void debug_flush_console()
{
    char * p=exception_text;
    while(*p){
        putch(*p);
        console_putch(*p++);
    }
    exception_text[0]='\0';
}

static int ptr_seems_valid(void * p){
	return (unsigned int)p>=0x80000000 && (unsigned int)p<0xa0000000;
}
typedef struct _framerec {
	struct _framerec *up;
	void *lr;
} frame_rec, *frame_rec_t;
#define CPU_STACK_TRACE_DEPTH		10

/* adapted from libogc exception.c */
static void debug_cpu_print_stack(void *pc,void *lr,void *r1)
{
	register unsigned int i = 0;
	register frame_rec_t l,p = (frame_rec_t)lr;

	l = p;
	p = r1;
	
	if (!ptr_seems_valid(p)) return;
	
	sprintf(exception_text,"%s\nSTACK DUMP:",exception_text);

	for(i=0;i<CPU_STACK_TRACE_DEPTH-1 && ptr_seems_valid(p->up);p=p->up,i++) {
		if(i%4) sprintf(exception_text,"%s --> ",exception_text);
		else {
			if(i>0) sprintf(exception_text,"%s -->\n",exception_text);
			else sprintf(exception_text,"%s\n",exception_text);
		}

		switch(i) {
			case 0:
				if(pc) sprintf(exception_text,"%s%p",exception_text,pc);
				break;
			case 1:
				sprintf(exception_text,"%s%p",exception_text,(void*)l);
				break;
			default:
				if(p && p->up) sprintf(exception_text,"%s%p",exception_text,
                                        (unsigned int)(p->up->lr));
				break;
		}
	}
}
static char *exception_strings[] =
{
    "Unknown Exception!",
    "Debug Print!", // Why are we here? this is handled...
    "Trap!",
    "Invalid Instruction!",
    "Invalid Float Operation!",
    "Privileged Instruction!",
    "Segmentation Fault! (Read)",
    "Segmentation Fault! (Write)",
    "Breakpoint!",
};

void dump_thread_context_to_screen(PROCESSOR_DATA_BLOCK *processor,
        unsigned int exceptionCode, CONTEXT *context)
{
    console_set_colors(0x000080ff, 0xffffffff);
    console_clrscr();
    
    strcpy(exception_text, exception_strings[exceptionCode]);
    
    debug_flush_console();

    sprintf(exception_text,"\nIar=%016llx Msr=%016llx Lr=%016llx Dar=%08X Pir=%02X\n\n",
                    context->Iar, context->Msr, context->Lr,
                        processor->DAR, (unsigned int)mfspr(pir));
    
    debug_flush_console();

    int i;
    for(i=0;i<8;++i)
            sprintf(exception_text,"%s%02d=%016llx %02d=%016llx %02d=%016llx %02d=%016llx\n",
                            exception_text,
                    i,context->Gpr[i],
                    i+8,context->Gpr[i+8],
                    i+16,context->Gpr[i+16],
                    i+24,context->Gpr[i+24]);

    debug_flush_console();
    
    debug_cpu_print_stack((void*)context->Iar,(void*)context->Lr,(void*)context->Gpr[1]);

    strcat(exception_text,"\n\nOn uart: 'h'=Halt, 'r'=Reboot\n\n");

    debug_flush_console();

    for(;;){
            switch(getch()){
                    case 'h':
                            xenon_smc_power_shutdown();
                            for(;;);
                            break;
                    case 'r':
                            xenon_smc_power_reboot();
                            for(;;);
                            break;
            }
    }
}

extern void (*stdout_hook)(const char *text, int len);
unsigned int debug_routine_stub(unsigned int code, CONTEXT *context)
{
    if(code != EXCEPT_CODE_DEBUG_PRINT)
        return 0;
    
    size_t i;
    size_t len = context->Gpr[4];
    char * src = (char*)context->Gpr[3];
    
    if (stdout_hook)
        stdout_hook(src, len);
    
    for (i = 0; i < len; ++i)
        putch(((const char*)src)[i]);
    
    // Skip over the trap
    context->Iar += 4;
    
    return 1;
}

// Returns exception code
unsigned int decode_exception(PROCESSOR_DATA_BLOCK *processor, unsigned int exceptReason)
{
    if(exceptReason == EXCEPT_REASON_INVALID_INSTRUCTION)
        return EXCEPT_CODE_INVALID_INSTRUCTION;
    else if(exceptReason == EXCEPT_REASON_INVALID_FLOAT_OPERATION)
        return EXCEPT_CODE_INVALID_FLOAT_OPERATION;
    else if(exceptReason == EXCEPT_REASON_PRIVILEGED_INSTRUCTION)
        return EXCEPT_CODE_PRIVILEGED_INSTRUCTION;
    else if(exceptReason == EXCEPT_REASON_TRAP)
    {
        // If you have any special reasons for trap codes, they go here
        // The format of the instruction is:
        // twi 31, r0, CODE
        
        unsigned int op = *(unsigned int*)processor->IARSave;
        // First, check if we handle this type of trap, if not just return trap
        if((op & 0xFFFFFF00) != 0x0FE00000) // twi 31, r0, 0
            return EXCEPT_CODE_TRAP;
        
        // Extract the trap type
        unsigned int trap_type = op & 0xFF;
        
        if(trap_type == DEBUG_TRAP_PRINT)
            return EXCEPT_CODE_DEBUG_PRINT;
        
        // Not defined, just trap
        return EXCEPT_CODE_TRAP;
    }
    
    // No other except reasons exist, in this case just return unknown exception
    return EXCEPT_CODE_UNKNOWN;
}

static unsigned int program_interrupt_lock = 0;

void segfault_handler(unsigned int address, unsigned int write,
        PROCESSOR_DATA_BLOCK *hvcontext)
{
    // This is called when a program segfaults
    // If write = 1, it tried to write to the address
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // Local stack context
    CONTEXT context;
    
    // If the exception was handled
    unsigned int handled = 0;
    
    // If we are first entering this handler
    unsigned int recursionCheck = 1;
    
    // Make sure only one exception gets in at a time
    if(processor->ExceptionRecursion == 0)
        lock(&program_interrupt_lock);
    else
        recursionCheck = 0;
    processor->ExceptionRecursion = 1;
    
    // Save the context
    dump_thread_context(&context);
    memcpy(context.Gpr, hvcontext->RegisterSave, 32*8);
    context.Cr = hvcontext->CRSave;
    context.Ctr = hvcontext->CTRSave;
    context.Iar = hvcontext->IARSave;
    context.Lr = hvcontext->LRSave;
    context.Msr = hvcontext->MSRSave;
    context.Xer = hvcontext->XERSave;
    
    // Adjust the context pointer
    mtsprg1(mfsprg1() - 0x200);
    
    processor->DAR = address;

    unsigned int code = write ?
        EXCEPT_CODE_SEGMENTATION_FAULT_WRITE
        : EXCEPT_CODE_SEGMENTATION_FAULT_READ;
    
    // Dispatch
    if(debugRoutine)
        handled = debugRoutine(code, &context);
    
    // Crash dump
    if(handled == 0)
        dump_thread_context_to_screen(processor, code, &context);
    
    // Restore context
    restore_thread_context(&context);
    
    processor->ExceptionRecursion = 0;
    if(recursionCheck)
        unlock(&program_interrupt_lock);
}

void program_interrupt_handler()
{
    // This is called when the program interrupts
    // Via an invalid instruction, a twi instruction, or what-have-you
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // Local stack context
    CONTEXT context;
    
    // If the exception was handled
    unsigned int handled = 0;
    
    // If we are first entering this handler
    unsigned int recursionCheck = 1;
    
    // Make sure only one exception gets in at a time
    if(processor->ExceptionRecursion == 0)
        lock(&program_interrupt_lock);
    else
        recursionCheck = 0;
    processor->ExceptionRecursion = 1;
    
    // Save the context
    dump_thread_context(&context);
    
    // Decode
    unsigned int code = decode_exception(processor, (processor->MSRSave >> 17) & 0xF);
    
    // Dispatch
    if(debugRoutine)
        handled = debugRoutine(code, &context);
    
    // Crash dump
    if(handled == 0)
        dump_thread_context_to_screen(processor, code, &context);
    
    // Restore context
    restore_thread_context(&context);
    
    processor->ExceptionRecursion = 0;
    if(recursionCheck)
        unlock(&program_interrupt_lock);
}

// Debug output printf, outputs up to 0x200 characters
int printf(const char * string, ...)
{
    char buf[0x200];
    va_list ap;
    int r;
    
    va_start(ap, string);
    r = vsnprintf(buf, 0x200, string, ap);
    va_end(ap);
    
    debug_print(buf, r);
    
    return r;
}