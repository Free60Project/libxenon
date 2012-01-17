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

// Create a mutex object
MUTEX* mutex_create(unsigned int max_lock_count)
{
    // Allocate
    MUTEX *mutex = (MUTEX*)malloc(sizeof(MUTEX));
    if(mutex == NULL)
        return NULL;
    
    // Zero and set the lock count
    memset(mutex, 0, sizeof(MUTEX));
    mutex->MaximumLockCount = max_lock_count;
    
    // Return
    return mutex;
}

// Destroy a mutex object (Note: make sure noone is using the object before you do this)
void mutex_destroy(MUTEX *mutex)
{
    lock(&mutex->Lock);
    free(mutex);
}

// This sets the lock count of a mutex
void mutex_setlockcount(MUTEX *mutex, unsigned int lock_count)
{
    lock(&mutex->Lock);
    mutex->MaximumLockCount = lock_count;
    unlock(&mutex->Lock);
}

// Attempt to acquire the mutex (Timeout of INFINITE will wait forever)
// 0 = timeout, 1 = acquired
unsigned int mutex_acquire(MUTEX *mutex, int timeout)
{
    int acquired = 0;
    //printf("PreLock\n");
    lock(&mutex->Lock);
    
    if(mutex->CurrentLockCount < mutex->MaximumLockCount)
    {
        // We own this mutex
        acquired = 1;
        mutex->CurrentLockCount++;
        //printf("EasyButton\n");
    }
    else
    {
        // Add ourself to the list on the object
        PTHREAD pthr = thread_get_current();
        pthr->PreviousThreadMutex = mutex->LastWaiting;
        pthr->NextThreadMutex = mutex->FirstWaiting;
        if(mutex->FirstWaiting)
            mutex->FirstWaiting->PreviousThreadMutex = pthr;
        else
            mutex->FirstWaiting = pthr;
        if(mutex->LastWaiting)
            mutex->LastWaiting->NextThreadMutex = pthr;
        mutex->LastWaiting = pthr;
    }
    
    unlock(&mutex->Lock);
    
    if(acquired) {
        //printf("EasyExit\n");
        return 1;
    }
    
    // Wait for the timeout
    //printf("PreAquireSleep\n");
    thread_get_current()->WaitingForMutex = 1;
    thread_sleep(timeout);
    //printf("PostComa waiting=%i\n", thread_get_current()->WaitingForMutex);
    // Lock
    lock(&mutex->Lock);
    //unsigned int irql = thread_spinlock(&ThreadListLock);
    
    // Check the result
    //printf("PreCheckRes\n");
    if(thread_get_current()->WaitingForMutex)
    {
        acquired = 0;
        //printf("WeDidntGetIt\n");
        // Remove ourself from the mutex list
        PTHREAD pthr = thread_get_current();
        if (pthr->NextThreadMutex)
            pthr->NextThreadMutex->PreviousThreadMutex = pthr->PreviousThreadMutex;
        if (pthr->PreviousThreadMutex)
            pthr->PreviousThreadMutex->NextThreadMutex = pthr->NextThreadMutex;
        if(mutex->FirstWaiting == pthr)
            mutex->FirstWaiting = pthr->NextThreadMutex;
        if(mutex->LastWaiting == pthr)
            mutex->LastWaiting = pthr->PreviousThreadMutex;
        if(mutex->FirstWaiting == pthr)
            mutex->FirstWaiting = NULL;
        if(mutex->LastWaiting == pthr)
            mutex->LastWaiting = NULL;
        //printf(":/\n");
    }
    else {
        //printf("WeGotIt!\n");
        acquired = 1;
    }
    // Unlock
    //thread_unlock(&ThreadListLock, irql);
    unlock(&mutex->Lock);
    //printf("PostUnlock\n");
    // Exit
    return acquired;
}

// Release the mutex
void mutex_release(MUTEX *mutex)
{
    lock(&mutex->Lock);
    
    mutex->CurrentLockCount--;
    while(mutex->FirstWaiting && (mutex->CurrentLockCount < mutex->MaximumLockCount))
    {
        // Check for and wake up threads until we can fill the mutex
        // Or we run out of threads
        
        // Pull from the list
        PTHREAD pthr = mutex->FirstWaiting;
        mutex->FirstWaiting = pthr->NextThreadMutex;
        if(mutex->FirstWaiting == pthr)
            mutex->FirstWaiting = NULL;
        if(mutex->LastWaiting == pthr)
            mutex->LastWaiting = NULL;
        
        // Signal and wake the thread
        pthr->WaitingForMutex = 0;
        pthr->SleepTime = 0;
        
        // Increment the counter
        mutex->CurrentLockCount++;
    }
    unlock(&mutex->Lock);
}