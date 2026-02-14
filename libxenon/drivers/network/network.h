#ifndef __include_network_h
#define __include_network_h

#ifdef __cplusplus
extern "C" {
#endif

#include <lwip/netif.h>
#include <lwip/dhcp.h>

#define NETWORK_INIT_SUCCESS 0
#define NETWORK_INIT_DHCP_FAILURE 1
#define NETWORK_INIT_FAILURE -1

int  network_init();
void network_poll();
void network_print_config();

extern struct netif netif;

#ifdef __cplusplus
}
#endif

#endif