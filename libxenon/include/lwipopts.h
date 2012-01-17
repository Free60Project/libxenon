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

#define NO_SYS                  0
#define NO_SYS_NO_TIMERS		0
#define ETHARP_TRUST_IP_MAC		0
#define ETHARP_TCPIP_INPUT              1
#define LWIP_TCPIP_CORE_LOCKING 0

#define LWIP_CALLBACK_API       1
#define LWIP_TCP                1
#define LWIP_UDP                1
#define LWIP_DHCP               1
#define LWIP_SOCKET             1
#define LWIP_NETCONN            1
#define LWIP_NETIF_HOSTNAME     1
#define LWIP_NETIF_API          1

#define LWIP_PROVIDE_ERRNO      1
#define MEM_ALIGNMENT           4
#define MEM_SIZE                (4 * 1024 * 1024)
#define MEMP_NUM_PBUF           1024
#define MEMP_NUM_UDP_PCB        20
#define MEMP_NUM_TCP_PCB        20
#define MEMP_NUM_TCP_PCB_LISTEN 16
#define MEMP_NUM_TCP_SEG        128
#define MEMP_NUM_NETBUF         2
#define MEMP_NUM_NETCONN        4
#define MEMP_NUM_SYS_TIMEOUT    20
#define PBUF_POOL_SIZE          512
#define TCP_TTL                 255
#define TCP_QUEUE_OOSEQ         1
#define TCP_MSS                 1476
#define TCP_SND_BUF             (8 * TCP_MSS)
#define TCP_WND                 (4 * TCP_MSS)
#define ARP_QUEUEING            0
#define IP_FORWARD              0
#define IP_OPTIONS              1
#define DHCP_DOES_ARP_CHECK     1


#define LWIP_STATS_DISPLAY            1

#define LWIP_DEBUG_TIMERNAMES         1

#define LWIP_DEBUG                    1
#define LWIP_DBG_MIN_LEVEL            LWIP_DBG_LEVEL_ALL


#define DBG_ON                          LWIP_DBG_ON

#define ETHARP_DEBUG                    DBG_ON
#define NETIF_DEBUG                     DBG_ON
#define PBUF_DEBUG                      DBG_ON
#define API_LIB_DEBUG                   DBG_ON
#define API_MSG_DEBUG                   DBG_ON
#define SOCKETS_DEBUG                   DBG_ON
#define ICMP_DEBUG                      DBG_ON
#define IGMP_DEBUG                      DBG_ON
#define INET_DEBUG                      DBG_ON
#define IP_DEBUG                        DBG_ON
#define IP_REASS_DEBUG                  DBG_ON
#define RAW_DEBUG                       DBG_ON
#define MEM_DEBUG                       DBG_ON
#define MEMP_DEBUG                      DBG_ON
#define SYS_DEBUG                       DBG_ON
#define TIMERS_DEBUG                    DBG_ON
#define TCP_DEBUG                       DBG_ON
#define TCP_INPUT_DEBUG                 DBG_ON
#define TCP_FR_DEBUG                    DBG_ON
#define TCP_RTO_DEBUG                   DBG_ON
#define TCP_CWND_DEBUG                  DBG_ON
#define TCP_WND_DEBUG                   DBG_ON
#define TCP_OUTPUT_DEBUG                DBG_ON
#define TCP_RST_DEBUG                   DBG_ON
#define TCP_QLEN_DEBUG                  DBG_ON
#define UDP_DEBUG                       DBG_ON
#define TCPIP_DEBUG                     DBG_ON
#define PPP_DEBUG                       DBG_ON
#define SLIP_DEBUG                      DBG_ON
#define DHCP_DEBUG                      DBG_ON
#define AUTOIP_DEBUG                    DBG_ON
#define SNMP_MSG_DEBUG                  DBG_ON
#define SNMP_MIB_DEBUG                  DBG_ON
#define DNS_DEBUG                       DBG_ON

#endif /* __LWIPOPTS_H__ */
