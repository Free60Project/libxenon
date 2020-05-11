#include <stdio.h>

#include <ppc/timebase.h>

#include "lwipopts.h"
#include "lwip/debug.h"

#include "lwip/timers.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/sys.h"
#include "lwip/init.h"
#include "lwip/dhcp.h"
#include "lwip/stats.h"

#include "lwip/ip.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/tcp_impl.h"

struct netif netif;

ip_addr_t ipaddr, netmask, gateway;
static uint64_t now, last_tcp, last_dhcp_coarse, last_dhcp_fine, now2, dhcp_wait;

#define NTOA(ip) (int)((ip.addr>>24)&0xff), (int)((ip.addr>>16)&0xff), (int)((ip.addr>>8)&0xff), (int)(ip.addr&0xff)

extern void enet_poll(struct netif *netif);
extern err_t enet_init(struct netif *netif);

void network_poll();


void network_init()
{

#ifdef STATS
	stats_init();
#endif /* STATS */
	printf(" * initializing lwip 1.4.1...\n");

	last_tcp=mftb();
	last_dhcp_fine=mftb();
	last_dhcp_coarse=mftb();

	//printf(" * configuring device for DHCP...\r\n");
	/* Start Network with DHCP */
	IP4_ADDR(&netmask, 255,255,255,255);
	IP4_ADDR(&gateway, 0,0,0,0);
	IP4_ADDR(&ipaddr, 0,0,0,0);

	lwip_init();  //lwip 1.4.1

	printf(" * initializing NIC\n");
	if (!netif_add(&netif, &ipaddr, &netmask, &gateway, NULL, enet_init, ip_input)){
		printf(" ! netif_add failed!\n");
		return;
	}
	netif_set_default(&netif);

	printf(" * requesting dhcp...");
	//dhcp_set_struct(&netif, &netif_dhcp);
	dhcp_start(&netif);

	dhcp_wait=mftb();
	int i = 0;
	while (netif.ip_addr.addr==0 && i < 60) {
		network_poll();
		now2=mftb();
		if (tb_diff_msec(now2, dhcp_wait) >= 250){
			dhcp_wait=mftb();
			i++;
			if (i % 2)
				printf(".");
		}
	}

	if (netif.ip_addr.addr) {
		printf("success\n");
	} else {
		printf("failed\n");
		printf(" * now assigning a static ip\n");

		IP4_ADDR(&ipaddr, 192, 168, 1, 99);
		IP4_ADDR(&gateway, 192, 168, 1, 1);
		IP4_ADDR(&netmask, 255, 255, 255, 0);
		netif_set_addr(&netif, &ipaddr, &netmask, &gateway);
		netif_set_up(&netif);
	}
}

void network_poll()
{

	// sys_check_timeouts();

	now=mftb();
	enet_poll(&netif);

	if (tb_diff_msec(now, last_tcp) >= TCP_TMR_INTERVAL)
	{
		last_tcp=mftb();
		tcp_tmr();
	}

	if (tb_diff_msec(now, last_dhcp_fine) >= DHCP_FINE_TIMER_MSECS)
	{
		last_dhcp_fine=mftb();
		dhcp_fine_tmr();
	}

	if (tb_diff_sec(now, last_dhcp_coarse) >= DHCP_COARSE_TIMER_SECS)
	{
		last_dhcp_coarse=mftb();
		dhcp_coarse_tmr();
	}
}

void network_print_config()
{
	printf(" * network config: %d.%d.%d.%d / %d.%d.%d.%d\n",
		NTOA(netif.ip_addr), NTOA(netif.netmask));
	printf("              MAC: %02X%02X%02X%02X%02X%02X\n\n",
			netif.hwaddr[0], netif.hwaddr[1], netif.hwaddr[2],
			netif.hwaddr[3], netif.hwaddr[4], netif.hwaddr[5]);
}


