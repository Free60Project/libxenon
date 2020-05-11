//#include "lwipopts.h"
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/stats.h"
#include "lwip/timers.h"
#include "netif/etharp.h"

#include <time/time.h>
#include <xenon_nand/xenon_config.h>
#include <ppc/cache.h>
#include <pci/io.h>
#include <xenon_smc/xenon_gpio.h>

#define TX_DESCRIPTOR_NUM 0x10
#define RX_DESCRIPTOR_NUM 0x10
#define MTU 1528
#define MEM(x) (0x80000000|(long)(x))

struct enet_context
{
	volatile uint32_t *rx_descriptor_base;
	void *rx_receive_base;
	int rx_descriptor_rptr;

	volatile uint32_t *tx_descriptor_base;
	int tx_descriptor_wptr;
	void *tx_buffer_base;

	struct eth_addr *ethaddr;
};

static int enet_open(struct netif *ctx);
static struct pbuf *enet_linkinput(struct enet_context *context);
static err_t enet_linkoutput(struct netif *netif, struct pbuf *p);
//static err_t enet_output(struct netif *netif, struct pbuf *p, ip_addr_t *ipaddr);

static unsigned short phy_read(int addr)
{
	write32n(0xea001444, __builtin_bswap32((addr << 11) | 0x50));
	do {mdelay(1);} while (__builtin_bswap32(read32n(0xea001444)) & 0x10);
	return __builtin_bswap32(read32n(0xea001444)) >> 16;
}

static void phy_write(int addr, unsigned short data)
{
	write32n(0xea001444, __builtin_bswap32((addr << 11) | 0x70 | (data << 16)));
	do {mdelay(1);} while (__builtin_bswap32(read32n(0xea001444)) & 0x10);
}

static void wait_empty_tx(struct enet_context *context)
{
//	printf(">> CURRENT TX PTR: %08x\n", __builtin_bswap32(read32n(0xea00140c)));
	while (__builtin_bswap32(context->tx_descriptor_base[context->tx_descriptor_wptr * 4 + 1]) & 0x80000000); // busy
}

static inline int virt_to_phys(volatile void *ptr)
{
	return ((long)ptr) & 0x7FFFFFFF;
}

static void tx_data(struct enet_context *context, unsigned char *data, int len)
{
	wait_empty_tx(context);
	int descnr = context->tx_descriptor_wptr;
	volatile uint32_t *descr = context->tx_descriptor_base + descnr * 4;

//	printf("using tx descriptor %d\n", descnr);

//	printf("really before %08x %08x %08x %08x\n", descr[0], descr[1], descr[2], descr[3]);

	descr[0] = __builtin_bswap32(len);
	descr[2] = __builtin_bswap32(virt_to_phys(data));
	descr[3] = __builtin_bswap32(len | ((descnr == TX_DESCRIPTOR_NUM - 1) ? 0x80000000 : 0));
	descr[1] = __builtin_bswap32(0xc0230000); // ownership, ...

//	printf("data at %08x\n", virt_to_phys(data));

	memdcbst((void*)descr, 0x10);

//	printf("before %08x %08x %08x %08x\n", descr[0], descr[1], descr[2], descr[3]);

	write32n(0xea001400, read32n(0xea001400) | __builtin_bswap32(0x10)); // Q0_START

//	printf("after %08x %08x %08x %08x\n", descr[0], descr[1], descr[2], descr[3]);

	context->tx_descriptor_wptr++;
	context->tx_descriptor_wptr %= TX_DESCRIPTOR_NUM;

	write32n(0xea001410, __builtin_bswap32(0x00101c11));	// enable RX

//	printf("TX\n");
}

static void tx_init(struct enet_context *context, void *base)
{
	int i;

	context->tx_descriptor_base = base;
	context->tx_descriptor_wptr = 0;

	for (i=0; i < TX_DESCRIPTOR_NUM; ++i)
	{
				/* tx descriptors */
		context->tx_descriptor_base[i * 4 + 0] = 0;
		context->tx_descriptor_base[i * 4 + 1] = 0;
		context->tx_descriptor_base[i * 4 + 2] = 0;
		if (i != TX_DESCRIPTOR_NUM - 1)
			context->tx_descriptor_base[i * 4 + 3] = 0;
		else
			context->tx_descriptor_base[i * 4 + 3] = __builtin_bswap32(0x80000000);
	}

	memdcbst((void*)context->tx_descriptor_base, TX_DESCRIPTOR_NUM * 0x10);

	context->tx_buffer_base = base + TX_DESCRIPTOR_NUM * 0x10;

	write32n(0xea001400, __builtin_bswap32(0x00001c00));
	write32n(0xea001404, __builtin_bswap32(virt_to_phys(context->tx_descriptor_base)));
	write32n(0xea001400, __builtin_bswap32(0x00011c00));
	write32n(0xea001404, __builtin_bswap32(virt_to_phys(context->tx_descriptor_base)));
	write32n(0xea001400, __builtin_bswap32(0x00001c00));
}


static void rx_init(struct enet_context *context, void *base)
{
	int i;

	context->rx_descriptor_base = base;
	context->rx_receive_base = base + RX_DESCRIPTOR_NUM * 0x10;
	context->rx_descriptor_rptr = 0;

			/* setup rx descriptors */
	for (i=0; i < RX_DESCRIPTOR_NUM; ++i)
	{
		context->rx_descriptor_base[i * 4 + 0] = __builtin_bswap32(0);
		context->rx_descriptor_base[i * 4 + 1] = __builtin_bswap32(0xc0000000);
		context->rx_descriptor_base[i * 4 + 2] = __builtin_bswap32(virt_to_phys(context->rx_receive_base + i * MTU));
		context->rx_descriptor_base[i * 4 + 3] = __builtin_bswap32(MTU | ((i == RX_DESCRIPTOR_NUM - 1) ? 0x80000000 : 0));
	}
	memdcbst((void*)context->rx_descriptor_base, RX_DESCRIPTOR_NUM * 0x10);

	write32n(0xea001414, __builtin_bswap32(virt_to_phys(context->rx_descriptor_base))); // RX queue start
	write32n(0xea001440, 0x1100004);
	write32n(0xea001450, 0);
}


//static void arp_timer(void *arg)
//{
//	etharp_tmr();
//	//sys_timeout(ARP_TMR_INTERVAL, arp_timer, NULL);
//}

err_t enet_init(struct netif *netif)
{
	struct enet_context * context;

	context = (struct enet_context *)mem_malloc(sizeof(struct enet_context));
	if(context == NULL) {
		printf("enet: Failed to allocate context memory.\n");
		return ERR_MEM;
	} else {
		memset(context, 0, sizeof(struct enet_context));

		// enet_write_mac_address(context, m);
		xenon_config_get_mac_addr(&netif->hwaddr[0]);
	}

	netif->state = context;
	netif->name[0] = 'e';
	netif->name[1] = 'n';
	netif->output = etharp_output; //enet_output; //lwip 1.3.0
	netif->linkoutput = enet_linkoutput;
	context->ethaddr = (struct eth_addr *)&(netif->hwaddr[0]);

	netif->hwaddr_len = 6;
	netif->mtu = 1500;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

#if LWIP_NETIF_HOSTNAME
    netif->hostname = "XeLL";
#endif

	//printf("NETIF at %p\n", netif);

	enet_open(netif);
	etharp_init();
	//sys_timeout(ARP_TMR_INTERVAL, arp_timer, NULL);
	return ERR_OK;
}


static int enet_open(struct netif *netif)
{
	int tries=10;
	struct enet_context *context = (struct enet_context *) netif->state;

	//printf("NIC reset\n");
	write32n(0xea001424, 0); // disable interrupts
	write32n(0xea001428, 0x01805508);
	//printf("reset: %08x\n", read32n(0xea001428));
    udelay(100);
	write32n(0xea001428, 0x01005508);

	write32n(0xea001444, __builtin_bswap32(4));
    udelay(100);
	write32n(0xea001444, __builtin_bswap32(0));
	//printf("1478 before: %08x\n", read32n(0xea001478));
		// Set MTU
	write32n(0xea001478, 0xf2050000); //printf("1478: %08x\n", read32n(0xea001478));

		// important
	write32n(0xea001450, 0x60230000);

		// mac 1+2
	write32n(0xea001478, 0xF2050000 | (netif->hwaddr[0] << 8) | (netif->hwaddr[1] << 0));
	write32n(0xea00147c, (netif->hwaddr[2] << 24) | (netif->hwaddr[3] << 16) | (netif->hwaddr[4] << 8) | (netif->hwaddr[5] << 0) );

	write32n(0xea001460, 0x380e0000 | (netif->hwaddr[0] << 8) | (netif->hwaddr[1] << 0));
	write32n(0xea001464, (netif->hwaddr[2] << 24) | (netif->hwaddr[3] << 16) | (netif->hwaddr[4] << 8) | (netif->hwaddr[5] << 0) );

		// multicast hash
	write32n(0xea001468, 0);
	write32n(0xea00146c, 0);

	write32n(0xea001400, 0x001c0000);
	write32n(0xea001410, __builtin_bswap32(0x00101c00));

	write32n(0xea001440, 0x01190004);

	void *base = (void*)memalign(0x10000,0x10000);

	//printf("init tx\n");
	tx_init(context, (void*)MEM(base));
	//printf("init rx\n");
	rx_init(context, (void*)MEM(base + 0x8000));

	write32n(0xea001440, 0x01100004);

#if 1
    printf("Reinit PHY...\n");

	// get PHY out of reset state
    xenon_gpio_control(0,0,0x10);
    xenon_gpio_control(4,0,0x10);

    phy_write(0, 0x9000);

	while (phy_read(0) & 0x8000){
		mdelay(500);
		tries--;
		if (tries<=0) break;		
	};

//	phy_write(0x10, 0x8058);
//	phy_write(4, 0x5e1);
//	phy_write(0x14, 0x4048);

	int linkstate = phy_read(1);
	if (!(linkstate & 4))
	{
		tries=10;

		printf("Waiting for link...");
		while (!(phy_read(1) & 4)){
			mdelay(500);
			tries--;
			if (tries<=0) break;
		};
	}

	if (phy_read(1) & 4)
		printf("link up!\n");
	else
		printf("link still down.\n");
#endif


//	write32n(0xea001428, 0x01805508);
	write32n(0xea001428, 0x01005508);
	write32n(0xea001410, __builtin_bswap32(0x00101c11));	// enable RX
	write32n(0xea001400, __builtin_bswap32(0x00001c01));	// enable TX

	return 0;
}

static struct pbuf *enet_linkinput(struct enet_context *context)
{
	volatile uint32_t *d = context->rx_descriptor_base + context->rx_descriptor_rptr * 4;
	memdcbf((void*)d, 0x10);
	if (__builtin_bswap32(d[1]) & 0x80000000) /* check ownership */
	{
//		printf("no data!\n");
		return 0;
	}

//	printf("RX descriptor %d contains data!\n", context->rx_descriptor_rptr);
//	printf("%08x %08x %08x %08x\n",
//		__builtin_bswap32(d[0]),
//		__builtin_bswap32(d[1]),
//		__builtin_bswap32(d[2]),
//		__builtin_bswap32(d[3]));

	int size = __builtin_bswap32(d[0]) & 0xFFFF;
	void *phys_addr = context->rx_receive_base + context->rx_descriptor_rptr * MTU;
	memdcbf(phys_addr, size);

	// hexdump(phys_addr, size);

	struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL), *q;

	if (p != NULL) {
		int rptr = 0;
		/* We iterate over the pbuf chain until we have read the entire
		 packet into the pbuf. */
		for(q = p; q != NULL; q = q->next) {
				/* Read enough bytes to fill this pbuf in the chain. The
					 available data in the pbuf is given by the q->len
					 variable. */
			memcpy(q->payload, phys_addr + rptr, q->len);
			rptr += q->len;
		}
#ifdef LINK_STATS
		lwip_stats.link.recv++;
#endif /* LINK_STATS */
	} else {
#ifdef LINK_STATS
		lwip_stats.link.memerr++;
		lwip_stats.link.drop++;
#endif /* LINK_STATS */
	}

		/* reclaim descriptor */
	d[2] = __builtin_bswap32(virt_to_phys(context->rx_receive_base + context->rx_descriptor_rptr * MTU));
	d[3] = __builtin_bswap32(MTU | ((context->rx_descriptor_rptr == RX_DESCRIPTOR_NUM - 1) ? 0x80000000 : 0));
	d[0] = __builtin_bswap32(0);
	d[1] = __builtin_bswap32(0xc0000000);
	memdcbst((void*)d, 0x10);

	context->rx_descriptor_rptr++;
	context->rx_descriptor_rptr %= RX_DESCRIPTOR_NUM;

	return p;
}

static err_t enet_linkoutput(struct netif *netif, struct pbuf *p)
{
	struct pbuf *q;
	struct enet_context *context = (struct enet_context *) netif->state;

//	printf("enet linkoutput\n");

	void *dstptr = context->tx_buffer_base + context->tx_descriptor_wptr * MTU;
	int offset = 0;

	for(q = p; q != NULL; q = q->next) {
		memcpy(dstptr + offset, q->payload, q->len);
		offset += q->len;
	}

	memdcbst((void*)dstptr, p->tot_len);
	tx_data(context, dstptr, p->tot_len);

//	printf("ok, data transmitted!\n");

	return 0;
}

/*
static err_t
enet_output(struct netif *netif, struct pbuf *p,
			struct ip_addr_t *ipaddr)
{
//	printf("ENET OUTPUT\n");
  //return etharp_output(netif, ipaddr, p);
  return etharp_output(netif, p, ipaddr); //lwip 1.3.0
}
*/

static void
enet_input(struct netif *netif)
{
	struct enet_context *context = (struct enet_context *) netif->state;
	struct eth_hdr *ethhdr;
	struct pbuf *p;

	p = enet_linkinput(context);

	if (p == NULL)
		return;

#if LINK_STATS
	lwip_stats.link.recv++;
#endif /* LINK_STATS */

	ethhdr = p->payload;

	/* pass to network layer */
	if (ethernet_input(p, netif) != ERR_OK) {
		pbuf_free(p);
		p = NULL;
	}
}

int cnt;

void
enet_poll(struct netif *netif)
{
//	printf("\r%08x", mfdec());
	enet_input(netif);
}


void enet_quiesce(void)
{
	write32n(0xea001400, 0);
	write32n(0xea001410, 0);
}
