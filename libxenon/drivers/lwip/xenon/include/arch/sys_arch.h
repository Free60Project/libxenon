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
 * $Id: sys_arch.h,v 1.1 2007/03/19 20:10:29 tmbinc Exp $
 */
#ifndef __SYS_C64_H__
#define __SYS_C64_H__

#include <threads/mutex.h>
#include <threads/threads.h>
//#include <lwip/timers.h>

/* let sys.h use binary semaphores for mutexes */
#define LWIP_COMPAT_MUTEX 1

#define SYS_SEM_NULL  NULL
#define SYS_MBOX_NULL NULL

#define sys_sem_valid(sema) ((sema != NULL) && ((sema)->sem != NULL))
#define sys_sem_set_invalid(sema) ((sema)->sem = NULL)
#define sys_mbox_valid(mbox) ((mbox != NULL) && ((mbox)->sem != NULL))
#define sys_mbox_set_invalid(mbox) ((mbox)->sem = NULL)


struct _sys_sem {
    MUTEX *sem;
};
typedef struct _sys_sem sys_sem_t;

#ifndef MAX_QUEUE_ENTRIES
#define MAX_QUEUE_ENTRIES 100
#endif
struct lwip_mbox {
  MUTEX *sem;
  void *q_mem[MAX_QUEUE_ENTRIES];
  u32_t head, tail;
};
typedef struct lwip_mbox sys_mbox_t;

struct _sys_thread_data {
    struct sys_timeo *timeouts;
} sys_thread_data_t;
//only need a PTHREAD, other bits stored in the thread itself
typedef PTHREAD sys_thread_t;

#endif /* __SYS_C64_H__ */
