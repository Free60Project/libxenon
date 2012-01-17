#include <threads/threads.h>
#include <ppc/register.h>
#include <time/time.h>
#include <xenon_soc/xenon_io.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_soc/xenon_secotp.h>
#include <xenon_smc/xenon_smc.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <time/time.h>
#include <ppc/atomic.h>
#include <string.h>
#include <stdlib.h>
#include <threads/mutex.h>

char processor_blocks[0x1000 * 6] = {0}; // Per-Processor Core Data Block
char processor_fpu_vpu_blocks[sizeof(PROCESSOR_FPU_VPU_SAVE) * 6] = {0};
char processor_interrupt_stack[0x1000 * 6] = {0}; // interrupt handling stack

// Enough stack for the other threads to init on
char thread_init_stack[0x1000 * 5];
static unsigned int threads_online = 0;

// Random Data
PROCESSOR_DATA_BLOCK *Processors[6] = {0}; // Array of all processor pointers
THREAD_LIST ThreadList; // List of all threads
THREAD ThreadPool[MAX_THREAD_COUNT]; // The thread pool

// Lock this if you ever have to lock more than one processor at once
// AND LOCK IT FIRST
unsigned int ThreadListLock = 0; // Lock for the thread list

// 20ms = 50000
static unsigned int decrementer_ticks = 50000; // Default decrementer value

int thread_spinlock(unsigned int *addr)
{
    char irql = thread_raise_irql(2);
    lock(addr);
    return irql;
}
void thread_unlock(unsigned int *addr, unsigned int irql)
{
    unlock(addr);
    thread_lower_irql(irql);
}

static void thread_interrupt_unexpected(unsigned int soc_interrupt)
{
    // Simple interrupt acknowledge
    std((volatile void*)(soc_interrupt + 0x60), 0);
}

unsigned long long _system_time = 0; // System Timer (Used for telling time)
unsigned long long _clock_time = 0; // Clock Timer (Used for timing stuff)
volatile unsigned long long _millisecond_clock_time = 0; // Millisecond Clock Timer
static void thread_interrupt_clock(unsigned int soc_interrupt)
{
    stw((volatile void*)0xEA00106C, 0x01000000);
    std((volatile void*)(soc_interrupt + 0x10), 0x3E0070);
    
    // Poll debugger
    
    // Update timers
    _clock_time += 10000;
    _system_time += 10000;
    _millisecond_clock_time += 1;
}

static void thread_interrupt_ipi_clock(unsigned int soc_interrupt)
{
}

static void thread_interrupt_ipi(unsigned int soc_interrupt)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // Process the Ipi
    lock(&processor->IpiLock);
    
    // Run the function
    if(processor->IpiProc)
    {
        processor->IpiProc(processor->IpiContext);
    
        // Clear the Ipi
        processor->IpiProc = NULL;
    }
    
    // Increment the counter
    if(processor->IpiIncrement)
        atomic_inc((unsigned int*)processor->IpiIncrement);
    
    unlock(&processor->IpiLock);
}

void hal_dpc_for_smc_request(void *Param1, void *Param2, void *Param3)
{
}

DPC hal_smc_request_dpc =
{
    hal_dpc_for_smc_request,
    NULL, NULL, NULL,
    DPC_PRIORITY_HIGH,
    NULL,
    0
};

static void thread_interrupt_hal_smm(unsigned int soc_interrupt)
{
    unsigned int hal_offset = 0xEA000000;
    
    // Fetch
    unsigned int message = __builtin_bswap32(lwz(hal_offset + 0x1050));
    
    // Check and queue
    
    // Acknowledge
    stw(hal_offset + 0x1058, __builtin_bswap32(message));
}

static void thread_interrupt_spurious(unsigned int soc_interrupt)
{
    // Don't do anything here
}

static void thread_interrupt_vdp_isr(unsigned int soc_interrupt)
{
    unsigned int vdp_offset = 0xEC800000;
    unsigned int fetch;
    
    fetch = lwz((volatile void*)(vdp_offset + 0xEDC));
    stw((volatile void*)(vdp_offset + 0x6540), 0);
    
    assert(!(fetch & 0x7FFFFFFE));
    if(fetch & 0x80000000)
    {
        stw((volatile void*)(vdp_offset + 0xED8), -1);
        fetch = lwz((volatile void*)(vdp_offset + 0xED4));
        if(fetch & 1)
        {
            fetch = lwz((volatile void*)(vdp_offset + 0xECC));
            
            char * name;
            if(fetch & 0x40000000)
                name = "Host";
            else
                name = "CP";
            
            save_floating_point(thread_get_processor_block()->FPUVPUSave);
            printf("RBBM read-error interrupt e:%d r:%d addr:%04X\n",
                    fetch >> 31, name, (fetch >> 2) & 0x7FFF);
            restore_floating_point(thread_get_processor_block()->FPUVPUSave);
        }
    }
}

void external_interrupt_handler()
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    unsigned int soc_interrupt =
        0x20050000 + (processor->CurrentProcessor * 0x1000);
    unsigned long long interruptSource
        = ld((volatile void*)(soc_interrupt + 0x50));
    
    char irq = processor->Irq;
    if((unsigned int)processor->InterruptTable[interruptSource / 4]
            != (unsigned int)thread_interrupt_unexpected)
    {
        // Set our Irq
        irq = processor->Irq;
        std((volatile void*)(soc_interrupt + 8), interruptSource);
        processor->Irq = interruptSource;
            ld((volatile void*)(soc_interrupt + 8));
    }
        else irq = 255;
    
    processor->InterruptTable[interruptSource / 4](soc_interrupt);
    
    if(irq != 255)
    {
        // Restore our Irq
        std((volatile void*)(soc_interrupt + 0x68), irq);
        processor->Irq = irq;
        ld((volatile void*)(soc_interrupt + 8));
    }
}

// Dumps the context from the processor to the current thread object
// As well as an optional pointer
void dump_thread_context(CONTEXT *context)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    PTHREAD pthr = processor->CurrentThread;
    
    if(context)
    {
        // Copy main regs
        memcpy(context->Gpr, processor->RegisterSave, 32*8);
        context->Msr = processor->MSRSave;
        context->Iar = processor->IARSave;
        context->Cr = processor->CRSave;
        context->Ctr = processor->CTRSave;
        context->Lr = processor->LRSave;
        context->Xer = processor->XERSave;
        
        // Dump floating point
        save_floating_point(&context->FpuVpu);
        
        if(pthr)
            memcpy(&pthr->Context, context, sizeof(CONTEXT));
        
        return;
    }
    
    if(pthr)
    {
        // Copy main regs
        memcpy(pthr->Context.Gpr, processor->RegisterSave, 32*8);
        pthr->Context.Msr = processor->MSRSave;
        pthr->Context.Iar = processor->IARSave;
        pthr->Context.Cr = processor->CRSave;
        pthr->Context.Ctr = processor->CTRSave;
        pthr->Context.Lr = processor->LRSave;
        pthr->Context.Xer = processor->XERSave;
    
        // Dump floating point
        save_floating_point(&pthr->Context.FpuVpu);

        // Dump vector
        //save_vector(&pthr->Context.FpuVpu); // UNUSED
    }
}

// Restores the current thread context to the processor
void restore_thread_context(CONTEXT *context)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    PTHREAD pthr = processor->CurrentThread;
    
    if(context)
    {
        if(pthr)
            memcpy(&pthr->Context, context, sizeof(CONTEXT));
        
        memcpy(processor->RegisterSave, context->Gpr, 32*8);
        processor->MSRSave = context->Msr;
        processor->IARSave = context->Iar;
        processor->CRSave = context->Cr;
        processor->CTRSave = context->Ctr;
        processor->LRSave = context->Lr;
        processor->XERSave = context->Xer;
            
        restore_floating_point(&context->FpuVpu);
        
        return;
    }
    
    if(pthr)
    {
        // Copy main regs
        memcpy(processor->RegisterSave, pthr->Context.Gpr, 32*8);
        processor->MSRSave = pthr->Context.Msr;
        processor->IARSave = pthr->Context.Iar;
        processor->CRSave = pthr->Context.Cr;
        processor->CTRSave = pthr->Context.Ctr;
        processor->LRSave = pthr->Context.Lr;
        processor->XERSave = pthr->Context.Xer;
    
        // Flush floating point
        restore_floating_point(&pthr->Context.FpuVpu);
    
        // Flush vector
        // restore_vector(&pthr->Context.FpuVpu); // UNUSED
    }
}

PTHREAD thread_schedule_core()
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    PTHREAD pthr = processor->FirstThread;
    THREAD_LIST readyList;
    
    readyList.FirstThread = readyList.LastThread = NULL;
    
    // First pass
    do
    {
        // Process timer
        if(pthr->SleepTime)
        {
            if(((signed long long)(pthr->SleepTime - _millisecond_clock_time)) < 0)
                pthr->SleepTime = 0;
        }
        // Check if the thread is no longer valid and has no references open
        // If so, free the resources
        if((pthr->ThreadTerminated == 1) && (pthr->HandleOpen == 0))
        {
            // Free the stack
            free(pthr->StackBase);
            // Remove from list
            pthr->NextThread->PreviousThread = pthr->PreviousThread;
            pthr->PreviousThread->NextThread = pthr->NextThread;
            pthr->NextThreadFull->PreviousThreadFull = pthr->PreviousThreadFull;
            pthr->PreviousThreadFull->NextThreadFull = pthr->NextThreadFull;
			// Make sure we dont explode
			if(pthr == processor->FirstThread)
				processor->FirstThread = pthr->NextThread;
			if(pthr == processor->LastThread)
				processor->LastThread = pthr->PreviousThread;
            // Mark as unused
            pthr->Valid = 0;
        }
        
        pthr = pthr->NextThread;
    } while(pthr != processor->FirstThread);

    // Check our swap process list
    lock(&processor->SwapProcessLock);
    while(processor->FirstSwapProcess != NULL)
    {
        // Swap this thread to our process
        PTHREAD thr = processor->FirstSwapProcess;
        thr->ThisProcessor = processor;
        thr->Context.Gpr[13] = (unsigned int)processor;
                
        // Remove from the swap list and put on the thread list
        if(thr->NextThread)
                thr->NextThread->PreviousThread = thr->PreviousThread;
        if(thr->PreviousThread)
                thr->PreviousThread->NextThread = thr->NextThread;
        processor->FirstSwapProcess = thr->NextThread;

        thr->NextThread = processor->FirstThread;
        thr->PreviousThread = processor->LastThread;
        processor->FirstThread->PreviousThread = thr;
        processor->LastThread->NextThread = thr;
        processor->FirstThread = thr;
    }
    unlock(&processor->SwapProcessLock);
    
    // Populate the ready list
    pthr = processor->FirstThread;
    do
    {
        if((pthr->ThreadTerminated == 0) && (pthr->SuspendCount == 0) && (pthr->SleepTime == 0) && pthr->Valid)
        {
            if(readyList.FirstThread == NULL)
            {
                readyList.FirstThread = readyList.LastThread = pthr;
                pthr->NextThreadReady = pthr;
                pthr->PreviousThreadReady = pthr;
            }
            else
            {
                pthr->NextThreadReady = readyList.FirstThread;
                pthr->PreviousThreadReady = readyList.LastThread;
                readyList.FirstThread->PreviousThreadReady = pthr;
                readyList.LastThread->NextThreadReady = pthr;
                readyList.FirstThread = pthr;
            }
        }
        pthr = pthr->NextThread;
    } while(pthr != processor->FirstThread);
   
    // It is VERY VERY important that the idle thread never sleep or get suspended
    // Otherwise we will be STUCK IN AN INFINITE LOOP
    
    // Walk the thread list, find the first thread with the highest priority
    pthr = readyList.FirstThread;
    PTHREAD winThread = pthr;
    do
    {
        if((pthr->Priority + pthr->PriorityBoost)
            > (winThread->Priority + winThread->PriorityBoost))
            winThread = pthr;
        else if(pthr->PriorityBoost < pthr->MaxPriorityBoost)
            pthr->PriorityBoost++; // Boost the priority if need be
        
        pthr = pthr->NextThreadReady;
    } while(pthr != readyList.FirstThread);
    
    return winThread;
}

// Processes the dpc list for the current processor
void thread_process_dpc()
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    lock(&processor->DPCLock);
    processor->ProcessingDPC = 1;
    
    if(processor->DPCList)
    {
        // Raise the irql to DPC level
        unsigned int irql = thread_raise_irql(2);
    
        // As our irql is 2, even though interrupts are enabled
        // We can't be rescheduled while processing this
        // However, external interrupts can and WILL interrupt us if they are asserted
        // Like the SMC going ( >o< Hey, Listen! )
    
        // Walk the DPC list
        DPC *dpc;
        while(processor->DPCList)
        {
            dpc = processor->DPCList;
            processor->DPCList = processor->DPCList->NextDPC;
            
            // Restore interrupts
            unsigned long long msr = mfmsr();
            mtmsr(msr | 0x8000);
            
            // Call the dpc
            dpc->Procedure(dpc->Param1, dpc->Param2, dpc->Param3);
            
            // Restore interrupts
            mtmsr(msr);
            
            // Unqueue the DPC
            dpc->NextDPC = NULL;
            dpc->Param1 = NULL;
            dpc->Param2 = NULL;
            dpc->Param3 = NULL;
            dpc->Queued = 0;
        }
    
            // Restore the irql
        thread_lower_irql(irql);
    }
    
    processor->ProcessingDPC = 0;
    unlock(&processor->DPCLock);
}

// Schedules for the current processes
void thread_schedule(CONTEXT *context)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // Lock the processor
    lock(&processor->Lock);
    
    // Mark current thread as not running
    processor->CurrentThread->ThreadIsRunning = 0;
    
    PTHREAD pthr = thread_schedule_core();
    
    // This thread is the winner, check the priority boost
    if(pthr->PriorityBoost)
        pthr->PriorityBoost--;
    
    // Mark as current thread
    processor->CurrentThread = pthr;
    
    // Mark as running
    pthr->ThreadIsRunning = 1;
    
    // Fetch the new context
    memcpy(context, &pthr->Context, sizeof(CONTEXT));
    
    // Unlock the processor
    unlock(&processor->Lock);
    
    // Clear any outstanding reservations
    // thread_clear_lwarx();
    
    // Process any DPCs
    if(processor->ProcessingDPC == 0)
        thread_process_dpc();
}

void decrementer_interrupt_handler()
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    CONTEXT context;
    
    // Flush the context
    dump_thread_context(&context);
    
    // Process any DPCs
    if(processor->Irq <= 2 && processor->ProcessingDPC == 0)
        thread_process_dpc();
    
    // We will only reschedule if the irq is less than 2 (DPC)
    if(processor->Irq < 2)
        thread_schedule(&context); // Quantum has ended!
    
    // Flush the context
    restore_thread_context(&context);
    
    // Reset the decrementer (used as the quantum timer)
    mtspr(dec, decrementer_ticks);
}

void system_call_handler()
{
    // Any system call generates a thread swap, regardless of your IRQL
    // (who needs syscalls? this entire thing runs in hv mode anyways)
    // Before swapping, it will set your IRQL to zero
    // Best used in functions that require a timer, or objects to be waited on
    CONTEXT context;
    
    // Flush the context
    dump_thread_context(&context);
    
    thread_get_processor_block()->Irq = 0;
    thread_schedule(&context);
    
    // Flush the context
    restore_thread_context(&context);
}

void thread_sleep(unsigned int milliseconds)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    unsigned long long sleep_time = _millisecond_clock_time + milliseconds;
    
    // Raise IRQ so we aren't rescheduled while setting this up
    thread_raise_irql(2);
    lock(&processor->Lock);
    
    // Set our sleep time
    processor->CurrentThread->SleepTime = sleep_time;
    
    unlock(&processor->Lock);
    
    // System call to signal thread swap
    asm volatile("sc");
}

void ipi_send_packet(thread_ipi_proc entrypoint,
        unsigned int context,
        char processors, unsigned volatile int *count)
{
    int i;
    
    // First set the ipi data
    for(i = 0;i < 6;i++)
    {
        if(((processors) & (1 << i)) == 0)
            continue;
        
        // Lock and set data
        lock(&Processors[i]->IpiLock);
        Processors[i]->IpiProc = entrypoint;
        Processors[i]->IpiContext = context;
        Processors[i]->IpiIncrement = count;
        unlock(&Processors[i]->IpiLock);
    }
    
    // Request the IPI interrupt
    unsigned int soc_interrupt =
        0x20050000 + (thread_get_processor_block()->CurrentProcessor * 0x1000);
    
    // Request interrupt 0x78 on the processor mask
    std((volatile void*)(soc_interrupt + 0x10), ((int)processors << 16) | 0x78);
}

// The IPI Lock
static unsigned int ipi_lock = 0;
unsigned int thread_send_ipi(thread_ipi_proc entrypoint, unsigned int context)
{   
    // How many have finished the IPI
    unsigned volatile int ipi_count = 0;
    
    // Return value
    unsigned int rval = 0;
    
    // Make sure we are running at dispatch level
    char irql;
    if(thread_get_processor_block()->Irq < 2)
        irql = thread_raise_irql(2);
    else
        irql = thread_get_processor_block()->Irq;
    
    // Make sure IPIs are one at a time
    lock(&ipi_lock);
    
    // Send the IPI to other processors
    ipi_send_packet(entrypoint, context,
            0x3F & ~(1 << thread_get_processor_block()->CurrentProcessor),
            &ipi_count);
    
    // Run the IPI ourselves
    thread_raise_irql(0x78);
    rval = entrypoint(context);
    
    // Wait for the other processors to finish
    while(ipi_count != 5)
        asm volatile("db16cyc");
    
    // Release the lock
    unlock(&ipi_lock);
    
    // Lower the irql and leave
    thread_lower_irql(irql);
    
    return rval;
}

PTHREAD thread_pool_alloc()
{
    int i;
    for(i = 0;i < MAX_THREAD_COUNT;i++)
    {
        if(!ThreadPool[i].Valid && !ThreadPool[i].HandleOpen)
        {
            memset(&ThreadPool[i], 0, sizeof(THREAD));
            ThreadPool[i].HandleOpen = 1;
            ThreadPool[i].Valid = 1;
            ThreadPool[i].ThreadId = i;
            
            printf("Thread %i Allocated\n", i);
            
            return &ThreadPool[i];
        }
    }
    
    return NULL; // Out of threads!
}

int thread_raise_irql(unsigned int irql)
{
    //assert(thread_get_processor_block()->Irq <= irql);
    unsigned int msr = thread_disable_interrupts();
    if(thread_get_processor_block()->Irq > irql)
    {
        printf("Thread %i tried to increase irq (%i) to a lesser value (%i)!\n",
                thread_get_processor_block()->CurrentProcessor,
                thread_get_processor_block()->Irq, irql);
    }
    
    char irq = thread_get_processor_block()->Irq;
    thread_get_processor_block()->Irq = irql;
    thread_enable_interrupts(msr);
    
    return irq;
}

int thread_lower_irql(unsigned int irql)
{
    //assert(thread_get_processor_block()->Irq >= irql);
    unsigned int msr = thread_disable_interrupts();
    if(thread_get_processor_block()->Irq < irql)
    {
        printf("Thread %i tried to lower irq (%i) to a greater value (%i)!\n",
                thread_get_processor_block()->CurrentProcessor,
                thread_get_processor_block()->Irq, irql);
    }
    
    char irq = thread_get_processor_block()->Irq;
    thread_get_processor_block()->Irq = irql;
    thread_enable_interrupts(msr);
    
    return irq;
}

void thread_terminate(unsigned int returnCode)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    // We dont ever want to leave this function, ever
    thread_raise_irql(2);
    
    lock(&processor->Lock);
    // Mark the thread as terminated, telling the scheduler to kill it off
    processor->CurrentThread->ThreadTerminated = 1;
    unlock(&processor->Lock);
    
    // Invoke the scheduler
    asm volatile("sc");
}

void thread_proc_startup(thread_proc entrypoint, void* argument)
{
    //printf("Thread Start %08X\n", thread_get_processor_block()->CurrentThread);
    int ret = entrypoint(argument);
    //printf("Thread End %08X\n", thread_get_processor_block()->CurrentThread);
    
    thread_terminate(ret);
}

PTHREAD thread_create(void* entrypoint, unsigned int stack_size,
        void* argument, unsigned int flags)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    PTHREAD pthr = NULL;
    
    // Lock the processor
    int irql = thread_spinlock(&processor->Lock);
    
    // Round up to nearest 4kb, min 16kb
    if(stack_size < 16*1024)
        stack_size = 16*1024;
    if(stack_size & 0x3FF)
        stack_size = (stack_size & 0xFFFFFC00) + 0x1000;
    
    char * stack = (char*)malloc(stack_size);
    
    if(stack != NULL)
    {
        // Get a thread object
        pthr = thread_pool_alloc();
        if(pthr != NULL)
        {
            // Setup thread vars
            pthr->StackBase = stack;
            pthr->StackSize = stack_size;
            pthr->Priority = 7; // Base priority
            pthr->MaxPriorityBoost = 5; // Default boost
            pthr->Valid = 1;
            
            pthr->ThisProcessor = thread_get_processor_block();
            
            // Insert into list
            unlock(&processor->Lock);
            lock(&ThreadListLock);
            lock(&processor->Lock);
            
            pthr->NextThread = processor->FirstThread;
            pthr->PreviousThread = processor->LastThread;
            processor->FirstThread->PreviousThread = pthr;
            processor->LastThread->NextThread = pthr;
            processor->FirstThread = pthr;
            
            pthr->NextThreadFull = ThreadList.FirstThread;
            pthr->PreviousThreadFull = ThreadList.LastThread;
            ThreadList.FirstThread->PreviousThreadFull = pthr;
            ThreadList.LastThread->NextThreadFull = pthr;
            ThreadList.FirstThread = pthr;
            
            unlock(&ThreadListLock);
            
            if(flags & THREAD_FLAG_CREATE_SUSPENDED)
                pthr->SuspendCount = 1;
            
            // Setup the registers
            pthr->Context.Gpr[1] = // Stack Ptr
                    (unsigned int)(pthr->StackBase + pthr->StackSize - 8);
            pthr->Context.Gpr[13] = (unsigned int)(thread_get_processor_block()); // PCR
            pthr->Context.Iar = (unsigned int)thread_proc_startup; // Address
            pthr->Context.Gpr[3] = (unsigned int)entrypoint; // Entry point
            pthr->Context.Gpr[4] = (unsigned int)argument; // Argument
            pthr->Context.Msr = 0x100000000200B030; // Machine State
        }
    }
    
    unlock(&processor->Lock);
    thread_lower_irql(irql);
    
    return pthr;
}

void thread_set_processor(PTHREAD pthr, unsigned int processor)
{
    unsigned int irql = thread_spinlock(&ThreadListLock);
    lock(&pthr->ThisProcessor->Lock);
    
    PROCESSOR_DATA_BLOCK *newProcess =
            (PROCESSOR_DATA_BLOCK*)(processor_blocks + processor * 0x1000);
    
    if(pthr->ThisProcessor != newProcess)
    {
        printf("pthr=%08X next=%08X previous=%08X\n",
                pthr, pthr->NextThread, pthr->PreviousThread);
        
        // Unlink the thread from its processor
        pthr->NextThread->PreviousThread = pthr->PreviousThread;
        pthr->PreviousThread->NextThread = pthr->NextThread;
        
        if(pthr->ThisProcessor->FirstThread == pthr)
            pthr->ThisProcessor->FirstThread = pthr->NextThread;
        if(pthr->ThisProcessor->LastThread == pthr)
            pthr->ThisProcessor->LastThread = pthr->PreviousThread;
        
        // Add to the new processor's swap list
        lock(&newProcess->SwapProcessLock);
        pthr->NextThread = newProcess->FirstSwapProcess;
        pthr->PreviousThread = newProcess->LastSwapProcess;
        if(newProcess->FirstSwapProcess)
                newProcess->FirstSwapProcess->PreviousThread = pthr;
        if(newProcess->LastSwapProcess)
                newProcess->LastSwapProcess->NextThread = pthr;
        newProcess->FirstSwapProcess = pthr;
            unlock(&newProcess->SwapProcessLock);
    }
    
    unlock(&pthr->ThisProcessor->Lock);
    thread_unlock(&ThreadListLock, irql);
    
    if(pthr == thread_get_current())
        asm volatile("sc"); // Invoke scheduler if we are swapping our process
}

void thread_close(PTHREAD pthr)
{
    int irql = thread_spinlock(&ThreadListLock);
    lock(&pthr->ThisProcessor->Lock);
    
    pthr->HandleOpen = 0;
    
    unlock(&pthr->ThisProcessor->Lock);
    thread_unlock(&ThreadListLock, irql);
}

int thread_suspend(PTHREAD pthr)
{
    int ret = -1;
    int irql = thread_spinlock(&ThreadListLock);
    lock(&pthr->ThisProcessor->Lock);
    
    if(pthr->SuspendCount < 80)
    {
        ret = pthr->SuspendCount;
        pthr->SuspendCount++;
    }
    
    unlock(&pthr->ThisProcessor->Lock);
    thread_unlock(&ThreadListLock, irql);
    
    return ret;
}

int thread_resume(PTHREAD pthr)
{
    int ret = -1;
    
    int irql = thread_spinlock(&ThreadListLock);
    lock(&pthr->ThisProcessor->Lock);
    
    if(pthr->SuspendCount)
    {
        ret = pthr->SuspendCount;
        pthr->SuspendCount--;
    }
    
    unlock(&pthr->ThisProcessor->Lock);
    thread_unlock(&ThreadListLock, irql);
    
    return ret;
}

void thread_set_priority_boost(PTHREAD pthr, unsigned int boost)
{
    unsigned int irql = thread_spinlock(&ThreadListLock);
    lock(&pthr->ThisProcessor->Lock);
    
    pthr->MaxPriorityBoost = boost;
    
    unlock(&pthr->ThisProcessor->Lock);
    thread_unlock(&ThreadListLock, irql);
}

void thread_set_priority(PTHREAD pthr, unsigned int priority)
{
    if(priority > 15)
    {
        printf("thread_set_priority: Tried to set priority above 15!\n");
        return;
    }
    
    unsigned int irql = thread_spinlock(&ThreadListLock);
    lock(&pthr->ThisProcessor->Lock);
    
    pthr->Priority = priority;
    
    unlock(&pthr->ThisProcessor->Lock);
    thread_unlock(&ThreadListLock, irql);
}

PTHREAD thread_get_current()
{
    PTHREAD pthr;
    unsigned int irql = thread_spinlock(&ThreadListLock);
    lock(&thread_get_processor_block()->Lock);
    
    pthr = thread_get_processor_block()->CurrentThread;
    
    unlock(&thread_get_processor_block()->Lock);
    thread_unlock(&ThreadListLock, irql);
    
    return pthr;
}

void thread_idle_loop()
{
    // Wait for everyone else
    while(threads_online !=  6)
        stall_execution(0xA);
    
    // Set the quantum length
    mtspr(dec, decrementer_ticks);
    
    // Enable interrupts
    mtmsr(mfmsr() | 0x8000);
    
    for(;;)
    {
        // We dont do much here
        asm volatile("db16cyc");
        
        // Invoke the scheduler by system calling
        asm volatile("sc");
    }
}

PTHREAD thread_create_idle_thread()
{
    PTHREAD pthr;
    static unsigned int thread_startup_lock = 0;
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    lock(&ThreadListLock);
    lock(&processor->Lock);
    lock(&thread_startup_lock);
    pthr = thread_pool_alloc();
    unlock(&thread_startup_lock);
    
    // Setup the thread
    pthr->ThisProcessor = processor;
    
    // Set current thread
    processor->CurrentThread = pthr;
    
    // Insert into list
    processor->FirstThread = processor->LastThread
            = pthr->NextThread = pthr->PreviousThread = pthr;
    
    if(processor->CurrentProcessor == 0)
    {
        // Setup the main thread list
        ThreadList.FirstThread = pthr;
        ThreadList.LastThread = pthr;
        
        // Set priority
        pthr->Priority = 7;
        pthr->MaxPriorityBoost = 5;
    }
    
    ThreadList.FirstThread->PreviousThreadFull = 
            ThreadList.LastThread->NextThreadFull = pthr;
    pthr->NextThreadFull = ThreadList.FirstThread;
    pthr->PreviousThreadFull = ThreadList.LastThread;
    
    unlock(&processor->Lock);
    unlock(&ThreadListLock);
    
    if(processor->CurrentProcessor == 0)
    {
        // We need to setup another thread for idling on this core
        PTHREAD pthr_idle = thread_create(thread_idle_loop, 0, NULL, 0);
        thread_set_priority(pthr_idle, 0);
        thread_set_priority_boost(pthr_idle, 0);
        thread_close(pthr_idle);
    }
    
    return pthr;
}

// This function sets up the processor state
void thread_startup()
{
    int i;
    
    // Init processor state
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    processor->Irq = 2;
    processor->FPUVPUSave =
            (PROCESSOR_FPU_VPU_SAVE*)(processor_fpu_vpu_blocks
            + processor->CurrentProcessor * sizeof(PROCESSOR_FPU_VPU_SAVE));
    
    // Setup interrupts
    for(i = 0;i < 0x20;i++)
        processor->InterruptTable[i] = thread_interrupt_unexpected;
    processor->InterruptTable[0x5] = thread_interrupt_hal_smm;
    processor->InterruptTable[0x16] = thread_interrupt_vdp_isr;
    if(processor->CurrentProcessor != 0)
        processor->InterruptTable[0x1C] = thread_interrupt_ipi_clock;
    else
        processor->InterruptTable[0x1D] = thread_interrupt_clock;
    processor->InterruptTable[0x1E] = thread_interrupt_ipi;
    processor->InterruptTable[0x1F] = thread_interrupt_spurious;
    
    // Set quantum length
    processor->QuantumEnd = decrementer_ticks;
    
    // Setup the idle thread
    thread_close(thread_create_idle_thread());
    
    // Signal ready
    atomic_inc(&threads_online);    
    
    // Allow thread scheduling on this core
    processor->Irq = 0;
    
    // Main thread can leave now
    if(processor->CurrentProcessor == 0)
        return;
    
    thread_idle_loop();
}

unsigned int quantum_length_ipi_routine(unsigned int context)
{
    // Just set the decrementer
    mtdec(context);
    return 0;
}

void process_set_quantum_length(unsigned int milliseconds)
{
    // Set the tick value
    decrementer_ticks = milliseconds * 2500;
    
    // Set the decrementer on all threads at once
    thread_send_ipi(quantum_length_ipi_routine, milliseconds * 2500);
}

static int threading_init_check = 0;

extern unsigned int wait[];
unsigned int thread_idle_ipi(unsigned int context)
{
    // Stop any interrupts from getting in
    thread_disable_interrupts();
    
    // Clear out the thread state
    wait[mfspr(pir) * 2] = 0;
    wait[mfspr(pir) * 2 + 1] = 0;
    
    for(;;)
    {
        asm volatile("or %r1, %r1, %r1");
        if(wait[mfspr(pir) * 2] != 0)
        {
            asm volatile("or %r2, %r2, %r2");
            
            // Set stack
            asm volatile("mr 1, %0" : : "r" (wait[mfspr(pir) * 2 + 1]));
            
            // Branch
            ((void(*)())wait[mfspr(pir) * 2])();
        }
    }
    
    return 0;
}

// Shuts down threading
void threading_shutdown()
{
    if(threading_init_check == 0
            || thread_get_processor_block()->CurrentProcessor != 0)
        return;
    
    thread_raise_irql(0x7C);
    thread_disable_interrupts();

    // Wait for pending ipis to clear
    lock(&ipi_lock);
    
    // Tell everyone to shut down
    unsigned int ipi_count = 0;
    ipi_send_packet(thread_idle_ipi, 0,
            0x3F & ~(1 << thread_get_processor_block()->CurrentProcessor),
            &ipi_count);
    
    // Wait for everyone to stop
    while(wait[2]);
    while(wait[4]);
    while(wait[6]);
    while(wait[8]);
    while(wait[10]);
}

// Starts up threading
void threading_init()
{
    int i;
    
    if(threading_init_check)
    {
        printf("Threading already init'd!\n");
        return;
    }
    threading_init_check = 1;
    
    // Init the base threading stuff
    xenon_thread_startup();
    
    Processors[0] = thread_get_processor_block();
    for(i = 0;i < 5;i++)
        Processors[i+1] = (PROCESSOR_DATA_BLOCK*)((char*)Processors[i] + 0x1000);
    
    // Wipe all the thread objects
    memset(ThreadPool, 0, sizeof(THREAD)*MAX_THREAD_COUNT);
    
    ThreadList.FirstThread = ThreadList.LastThread = NULL;
    
    // Set the cores to full speed
    xenon_make_it_faster(XENON_SPEED_FULL);
    
    // Main processor startup
    thread_startup();
    
    // Tell the other threads to run "thread_startup"
    for(i = 1;i < 6;i++)
    {
        int res = xenon_run_thread_task(i,
                thread_init_stack + ((i - 1) * 0x1000) + (0x1000-0x8),
                thread_startup);
        
        if(res)
                printf("Thread %i failed to run init\n", i);
    }
    
    // Wait for the threads to complete startup
    while(threads_online !=  6)
        stall_execution(0xA);
    
    // Set the quantum length
    mtspr(dec, decrementer_ticks);
    
    // Enable interrupts
    mtmsr(mfmsr() | 0x8000);
    
    printf("All threads online!\n");
    
    printf("Thread init complete!\n");
}

// Initialize a DPC
void dpc_initialize(DPC *dpc, dpc_proc procedure, unsigned int priority)
{
    memset(dpc, 0, sizeof(DPC));
    dpc->Priority = priority;
    dpc->Procedure = procedure;
}

// Queue a DPC to be called
unsigned int dpc_queue(DPC *dpc, void *param1, void *param2, void *param3)
{
    PROCESSOR_DATA_BLOCK *processor = thread_get_processor_block();
    
    unsigned int rval = 0;
    unsigned int irql = thread_spinlock(&ThreadListLock);
    
    // If we are processing a DPC while queueing, dont lock
    if(processor->ProcessingDPC == 0)
        lock(&processor->DPCLock);
    
    if(dpc->Queued == 0)
    {
        dpc->Param1 = param1;
        dpc->Param2 = param2;
        dpc->Param3 = param3;
        
        dpc->Queued = 1;
        
        rval = 1;
        if(processor->DPCList)
        {
            if(processor->DPCList->Priority < dpc->Priority)
            {
                // Stick on the front of the queue
                dpc->NextDPC = processor->DPCList;
                processor->DPCList = dpc;
            }
            else
            {
                DPC *list = processor->DPCList;
                for(;;)
                {
                    if(list->NextDPC == NULL)
                    {
                        list->NextDPC = dpc;
                        break;
                    }
                    
                    // Check priority of next on list
                    if(list->NextDPC->Priority < dpc->Priority)
                    {
                        // Insert into list
                        dpc->NextDPC = list->NextDPC;
                        list->NextDPC = dpc;
                        break;
                    }
                }
            }
        }
        else
            processor->DPCList = dpc;
    }
    
    if(processor->ProcessingDPC == 0)
        unlock(&processor->DPCLock);
    thread_unlock(&ThreadListLock, irql);
    
    return rval;
}