/*
 * Copyright (c) 2001, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 * $Id: sys_arch.c,v 1.1 2007/03/19 20:10:27 tmbinc Exp $
 */

#include <lwip/opt.h>
#include <lwip/arch.h>
#include <lwip/stats.h>
#include <lwip/debug.h>
#include <lwip/sys.h>

#include <ppc/timebase.h>
#include <threads/mutex.h>

u32_t startTime;

void sys_init(void)
{
    startTime = mftb();
    return;
}

/** Returns the current time in milliseconds,
 * may be the same as sys_jiffies or at least based on it. */
u32_t sys_now(void)
{
    return sys_jiffies();
}

u32_t sys_jiffies(void) /* since power up. */
{
    return (u32_t)tb_diff_msec(mftb(), startTime);
}

#if !NO_SYS

err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
    printf("sys_sem_new(count=%0u)\n", count);
    
    LWIP_ASSERT("sem != NULL", sem != NULL);

    //create the mutex (really a semaphore)
    MUTEX *mut = mutex_create(100000);
    
    LWIP_ASSERT("mut != NULL", mut != NULL);
    if(mut != NULL) {
        SYS_STATS_INC_USED(sem);
        //#if LWIP_STATS && SYS_STATS
        //LWIP_ASSERT("sys_sem_new() counter overflow", lwip_stats.sys.sem.used != 0 );
        //#endif /* LWIP_STATS && SYS_STATS*/
        mut->CurrentLockCount = (100000 - count);
        sem->sem = mut;
        return ERR_OK;
    }
    
    //Failed to allocate memory for mutex
    SYS_STATS_INC(sem.err);
    sem->sem = SYS_SEM_NULL;
    return ERR_MEM;
}

void sys_sem_free(sys_sem_t *sem)
{
    printf("sys_sem_free()\n");
    
    LWIP_ASSERT("sem != NULL", sem != NULL);
    LWIP_ASSERT("sem->sem != SYS_SEM_NULL", sem->sem != SYS_SEM_NULL);
    mutex_destroy(sem->sem);
    
    SYS_STATS_DEC(sem.used);
    sem->sem = NULL;
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
    printf("sys_arch_sem_wait(timeout=%0u)\n", timeout);
    
    LWIP_ASSERT("sem != NULL", sem != NULL);
    u32_t start, end;
    
    if (timeout == 0) {
        timeout = INFINITE;
    }
    
    start = sys_now();
    //make the mutex call
    unsigned int aqrd = mutex_acquire(sem->sem, timeout);
    
    if (aqrd == 0) {
        return SYS_ARCH_TIMEOUT;
    } else {
        end = sys_now();
        //return the elapsed time in ms
        return (end - start);
    }
}

void sys_sem_signal(sys_sem_t *sem)
{
    //Release the mutex
    printf("sys_sem_signal()\n");
    LWIP_ASSERT("sem != NULL", sem != NULL);
    LWIP_ASSERT("sem->sem != NULL", sem->sem != NULL);
    mutex_release(sem->sem);
}

err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
    printf("sys_mbox_new()\n");
    LWIP_ASSERT("mbox != NULL", mbox != NULL);
    LWIP_UNUSED_ARG(size);
    
    mbox->sem = mutex_create(MAX_QUEUE_ENTRIES);
    mbox->sem->CurrentLockCount = MAX_QUEUE_ENTRIES;
    LWIP_ASSERT("Error creating semaphore", mbox->sem != NULL);
    if(mbox->sem == NULL) {
        SYS_STATS_INC(mbox.err);
        return ERR_MEM;
    }
    memset(mbox->q_mem, 0, sizeof(u32_t)*MAX_QUEUE_ENTRIES);
    mbox->head = mbox->tail = 0;
    SYS_STATS_INC_USED(mbox);
    return ERR_OK;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
    printf("sys_mbox_free()\n");
    LWIP_ASSERT("mbox != NULL", mbox != NULL);
    LWIP_ASSERT("mbox->sem != NULL", mbox->sem != NULL);
    
    mutex_destroy(mbox->sem);
    
    SYS_STATS_DEC(mbox.used);
    mbox->sem = NULL;
}

//TODO: Make this follow the 'must not fail' rule
//Must wait infinitely for space in the queue if it is full
//Currently just writes regardless of where the tail is
void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
    printf("sys_mbox_post()\n");
    LWIP_ASSERT("mbox != SYS_MBOX_NULL", mbox != SYS_MBOX_NULL);
    LWIP_ASSERT("mbox->sem != NULL", mbox->sem != NULL);
    
    mbox->q_mem[mbox->head] = msg;
    (mbox->head)++;
    if (mbox->head >= MAX_QUEUE_ENTRIES) {
        mbox->head = 0;
    }
    LWIP_ASSERT("mbox is full", mbox->head != mbox->tail);
    mutex_release(mbox->sem);
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
    printf("sys_mbox_trypost()\n");
    LWIP_ASSERT("mbox != SYS_MBOX_NULL", mbox != SYS_MBOX_NULL);
    LWIP_ASSERT("mbox->sem != NULL", mbox->sem != NULL);
    
    u32_t new_head = mbox->head + 1;
    if (new_head >= MAX_QUEUE_ENTRIES) {
        new_head = 0;
    }
    if (new_head == mbox->tail) {
        printf("Head: %i\tTail: %i\tNewHead: %i\n", mbox->head, mbox->tail, new_head);
        printf("mbox full!\n");
        return ERR_MEM;
    }
    
    mbox->q_mem[mbox->head] = msg;
    mbox->head = new_head;
    LWIP_ASSERT("mbox is full!", mbox->head != mbox->tail);
    mutex_release(mbox->sem);
    return ERR_OK;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
    //printf("sys_mbox_fetch(timeout=%i)\n", timeout);
    LWIP_ASSERT("mbox != SYS_MBOX_NULL", mbox != SYS_MBOX_NULL);
    LWIP_ASSERT("mbox->sem != NULL", mbox->sem != NULL);
    
    if (timeout == 0) {
        timeout = INFINITE;
    }
    
    u32_t start, end;
    start = sys_now();
    unsigned int aqrd = mutex_acquire(mbox->sem, timeout);
    if (aqrd == 0) {
        if (msg != NULL) {
            *msg = NULL;
        }
        return SYS_ARCH_TIMEOUT;
    }
    if (msg != NULL) {
        *msg = mbox->q_mem[mbox->tail];
    } else {
        printf("msg null!, didnt assign the message pointer\n");
    }
    
    (mbox->tail)++;
    if (mbox->tail >= MAX_QUEUE_ENTRIES) {
        mbox->tail = 0;
    }
    end = sys_now();
    return (end - start);
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
    printf("sys_mbox_tryfetch()\n");
    LWIP_ASSERT("mbox != SYS_MBOX_NULL", mbox != SYS_MBOX_NULL);
    LWIP_ASSERT("mbox->sem != NULL", mbox->sem != NULL);
    
    //hacky, but timeout of 0 seems to sleep forever
    unsigned int aqrd = mutex_acquire(mbox->sem, 0);
    if (aqrd == 0) {
        if (msg != NULL) {
            *msg = NULL;
        }
        return SYS_MBOX_EMPTY;
    }
    if (msg != NULL) {
        *msg = mbox->q_mem[mbox->tail];
    }
    
    (mbox->tail)++;
    if (mbox->tail >= MAX_QUEUE_ENTRIES) {
        mbox->tail = 0;
    }
    return 0;
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio)
{
    printf("sys_thread_new(name=%s, stacksize=%i, prio=%i)\n", name, stacksize, prio);
    
    LWIP_UNUSED_ARG(name);
    LWIP_UNUSED_ARG(stacksize);
    LWIP_UNUSED_ARG(prio);
    
    //create the actual thread
    PTHREAD pthrd = thread_create(thread, stacksize, arg, prio);
    LWIP_ASSERT("pthrd != NULL", pthrd != NULL);
    thread_resume(pthrd);

    return (sys_thread_t)pthrd;
}

#endif /* !NO_SYS */