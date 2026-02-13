/*
 * Copyright (c) 2001, 2002 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */


#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

#include <string.h>
#include <stdio.h>

#define NO_SYS                  1
#define NO_SYS_NO_TIMERS        0
#define SYS_LIGHTWEIGHT_PROT    0
#define ETHARP_TRUST_IP_MAC     1

#define LWIP_CALLBACK_API       1
#define LWIP_TCP                1
#define LWIP_UDP                1
#define LWIP_DHCP               1
#define LWIP_SOCKET             0
#define LWIP_NETCONN            0
#define LWIP_NETIF_HOSTNAME     1

#define MEM_ALIGNMENT           4
#define MEM_SIZE                (4 * 1024 * 1024)
#define MEMP_NUM_PBUF           1024
#define MEMP_NUM_UDP_PCB        20
#define MEMP_NUM_TCP_PCB        20
#define MEMP_NUM_TCP_PCB_LISTEN 16
#define MEMP_NUM_TCP_SEG        128
#define MEMP_NUM_NETBUF         0
#define MEMP_NUM_NETCONN        0
#define MEMP_NUM_SYS_TIMEOUT    20
#define PBUF_POOL_SIZE          512
#define TCP_TTL                 255
#define TCP_QUEUE_OOSEQ         1
#define TCP_MSS                 1476
#define TCP_SND_BUF             (8 * TCP_MSS)
#define TCP_WND                 (4 * TCP_MSS)
#define ARP_QUEUEING            1
#define IP_FORWARD              0
#define IP_OPTIONS              1
#define DHCP_DOES_ARP_CHECK     1


//#define LWIP_DEBUG 					1
//#define DHCP_DEBUG                    LWIP_DBG_ON
//#define NETIF_DEBUG                   LWIP_DBG_ON
//#define TIMERS_DEBUG                  LWIP_DBG_ON
//#define LWIP_DEBUG_TIMERNAMES			1

#endif /* __LWIPOPTS_H__ */
