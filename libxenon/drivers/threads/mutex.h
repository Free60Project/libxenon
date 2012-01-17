#ifndef MUTEX_H
#define	MUTEX_H

#include <threads/threads.h>

#ifdef	__cplusplus
extern "C" {
#endif
    
#pragma pack(push, 1)

// The mutex object
typedef struct _MUTEX
{
    // How many are currently using this object
    unsigned int CurrentLockCount;
    
    // How many can use this object at once
    unsigned int MaximumLockCount;
    
    // The lock for this object (used to synchronize access)
    unsigned int Lock;
    
    // The list of threads waiting on this object
    struct _THREAD *FirstWaiting;
    struct _THREAD *LastWaiting;
} MUTEX;

#pragma pack(pop)

// Create a mutex object
MUTEX* mutex_create(unsigned int max_lock_count);
// Destroy a mutex object (Note: make sure noone is using the object before you do this)
void mutex_destroy(MUTEX *mutex);
// This sets the lock count of a mutex
void mutex_setlockcount(MUTEX *mutex, unsigned int lock_count);
// Attempt to acquire the mutex (Timeout of INFINITE will wait forever)
// 0 = timeout, 1 = acquired
unsigned int mutex_acquire(MUTEX *mutex, int timeout);
// Release the mutex
void mutex_release(MUTEX *mutex);

#ifdef	__cplusplus
}
#endif

#endif	/* MUTEX_H */

