/*
 * Named constants come from linux's sis190 driver: https://github.com/torvalds/linux/blob/v7.2/drivers/net/ethernet/sis/sis190.c
 */

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
#include <xb360/xb360.h>

#include "enet.h"

struct enet_context
{
	volatile struct RxDesc *rx_descriptor_base;
	void *rx_receive_base;
	int rx_descriptor_rptr;

	volatile struct TxDesc *tx_descriptor_base;
	int tx_descriptor_wptr;
	void *tx_buffer_base;

	struct eth_addr *ethaddr;

	int phy_id;
	int id[2];
};

static int enet_open(struct netif *ctx);
static struct pbuf *enet_linkinput(struct enet_context *context);
static err_t enet_linkoutput(struct netif *netif, struct pbuf *p);

static unsigned short phy_read(struct enet_context * context, int addr)
{
	SIS_W32(GMIIControl, EhnMIIreq | EhnMIIread |
		(addr << EhnMIIregShift) | (context->phy_id << EhnMIIpmdShift));
	do {mdelay(1);} while (SIS_R32(GMIIControl) & EhnMIInotDone);
	return SIS_R32(GMIIControl) >> EhnMIIdataShift;
}

static void phy_write(struct enet_context * context, int addr, unsigned short data)
{
	SIS_W32(GMIIControl, EhnMIIreq | EhnMIIwrite |
		(addr << EhnMIIregShift) | (context->phy_id << EhnMIIpmdShift) |
		((uint32_t)data << EhnMIIdataShift));
	do {mdelay(1);} while (SIS_R32(GMIIControl) & EhnMIInotDone);
}

static int phy_read_latched(struct enet_context * context, int reg)
{
    phy_read(context, reg);
    return phy_read(context, reg);
}

static int phy_link_up(struct enet_context * context)
{
    return ((phy_read_latched(context, MII_BMSR) & BMSR_LSTATUS) != 0);
}

static void wait_empty_tx(struct enet_context *context)
{
    // printf(">> CURRENT TX PTR: %08x\n", __builtin_bswap32(read32n(0xea00140c)));
	while (__builtin_bswap32(context->tx_descriptor_base[context->tx_descriptor_wptr].status) & OWNbit); // busy
}

static inline int virt_to_phys(volatile void *ptr)
{
	return ((long)ptr) & 0x7FFFFFFF;
}

static void tx_data(struct enet_context *context, unsigned char *data, int len)
{
	wait_empty_tx(context);

	int descnr = context->tx_descriptor_wptr;
	volatile struct TxDesc *descr = context->tx_descriptor_base + descnr;

	descr->PSize = __builtin_bswap32(len);
	descr->addr = __builtin_bswap32(virt_to_phys(data));
	descr->size = __builtin_bswap32(len | ((descnr == TX_DESCRIPTOR_NUM - 1) ? RingEnd : 0));
	descr->status = __builtin_bswap32(OWNbit | INTbit | DEFEN | CRCbit | PADbit);

	memdcbst((void*)descr, sizeof(struct TxDesc));

	SIS_W32(TxControl, SIS_R32(TxControl) | CmdReset); // kick TX queue 0

	context->tx_descriptor_wptr++;
	context->tx_descriptor_wptr %= TX_DESCRIPTOR_NUM;

	SIS_W32(RxControl, 0x00101c01 | CmdReset); // enable RX
}

static void tx_init(struct enet_context *context, void *base)
{
	int i;
	context->tx_descriptor_base = base;
	context->tx_descriptor_wptr = 0;

	for (i=0; i < TX_DESCRIPTOR_NUM; ++i)
	{
		context->tx_descriptor_base[i].PSize = 0;
		context->tx_descriptor_base[i].status = 0;
		context->tx_descriptor_base[i].addr = 0;
		if (i != TX_DESCRIPTOR_NUM - 1)
			context->tx_descriptor_base[i].size = 0;
		else
			context->tx_descriptor_base[i].size = __builtin_bswap32(RingEnd);
	}

	memdcbst((void*)context->tx_descriptor_base, TX_DESCRIPTOR_NUM * sizeof(struct TxDesc));

	context->tx_buffer_base = base + TX_DESCRIPTOR_NUM * sizeof(struct TxDesc);

	SIS_W32(TxControl, 0x00001c00);
	SIS_W32(TxDescStartAddr, virt_to_phys(context->tx_descriptor_base));
	SIS_W32(TxControl, 0x00011c00);
	SIS_W32(TxDescStartAddr, virt_to_phys(context->tx_descriptor_base));
	SIS_W32(TxControl, 0x00001c00);
}

static void rx_init(struct enet_context *context, void *base)
{
	int i;
	context->rx_descriptor_base = base;
	context->rx_receive_base = base + RX_DESCRIPTOR_NUM * sizeof(struct RxDesc);
	context->rx_descriptor_rptr = 0;

	for (i=0; i < RX_DESCRIPTOR_NUM; ++i)
	{
		context->rx_descriptor_base[i].PSize = __builtin_bswap32(0);
		context->rx_descriptor_base[i].status = __builtin_bswap32(OWNbit | INTbit);
		context->rx_descriptor_base[i].addr = __builtin_bswap32(virt_to_phys(context->rx_receive_base + i * MTU));
		context->rx_descriptor_base[i].size = __builtin_bswap32(MTU | ((i == RX_DESCRIPTOR_NUM - 1) ? RingEnd : 0));
	}

	memdcbst((void*)context->rx_descriptor_base, RX_DESCRIPTOR_NUM * sizeof(struct RxDesc));

	SIS_W32(RxDescStartAddr, virt_to_phys(context->rx_descriptor_base));
	SIS_W32(StationControl, 0x04001001);
	SIS_W32(TxMacControl, 0);
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

	// printf("NETIF at %p\n", netif);

	enet_open(netif);
	etharp_init();
	// sys_timeout(ARP_TMR_INTERVAL, arp_timer, NULL);
	return ERR_OK;
}

static int enet_probe(struct enet_context *context)
{
    int status = 0;
    for (int i=0; i < PHY_MAX_ADDR; i++) {
        context->phy_id = i;

        status = phy_read_latched(context, MII_BMSR);

        // Try next mii if the current one is not accessible.
	if (status == 0xffff || status == 0x0000)
		continue;

	context->id[0] = phy_read(context, MII_PHYSID1);
	context->id[1] = phy_read(context, MII_PHYSID2);
	return 0;
    }

    return 1;
}

/* PHY MDIO read-modify-write helper */
static void phy_modify(struct enet_context *context, int reg, unsigned short clear, unsigned short set)
{
	phy_write(context, reg, (phy_read(context, reg) & ~clear) | set);
}

/* Atheros debug register window: write index to reg 29, access data via reg 30 */
static void at803x_debug_modify(struct enet_context *context, int index, unsigned short clear, unsigned short set)
{
	phy_write(context, AT803X_DEBUG_ADDR, index);
	phy_modify(context, AT803X_DEBUG_DATA, clear, set);
}

static void enet_phy_setup(struct enet_context *context)
{
	switch (context->id[0]) {
	case PHYID_ICS:
		/*
		 * 0x8058 = Command Override Write Enable | PHY address 1 (read-only) | reserved
		 * bit 4 | NRZI. It arms the next write: bit 4.10 (pause) of the advertisement
		 * register is a command-override bit, so 0x05e1 (selector, 10/100 HD/FD, pause)
		 * only sticks right after it. Regs 20/21 are reserved by IDT, undocumented
		 */
		phy_write(context, ICS1893_EXTCTRL, ICS1893_CMD_OVERRIDE |
			(context->phy_id << ICS1893_PHYADDR_SHIFT) | 0x0010 | ICS1893_NRZI);
		phy_write(context, MII_ADVERTISE, 0x05e1);
		phy_write(context, ICS1893_RSVD_20, 0x4048);
		phy_write(context, ICS1893_RSVD_21, 0xa000);
		return;
	case PHYID_ATHEROS:
		at803x_debug_modify(context, AT803X_DEBUG_HIB_CTRL, AT803X_DEBUG_PS_HIB_EN, 0);
		at803x_debug_modify(context, 0x12, 0x0008, 0);
		at803x_debug_modify(context, 0x29, 0x0007, 0x0004);
		break;
	case PHYID_MRV_SLIM:
		if (xenon_get_PCIBridgeRevisionID() == 0x90) {
			phy_write(context, MII_BMCR, 0xa100);
			SIS_W32(GIoCR, SIS_R32(GIoCR) | 1);
			SIS_W32(GIoCR, SIS_R32(GIoCR) & ~1u);
		}
		break;
	case PHYID_BCM_PHAT:
	case PHYID_MICREL:
		break;
	default:
		printf("enet: unknown phy id %x\n", context->id[0]);
		break;
	}
	phy_modify(context, MII_ADVERTISE, 0, ADVERTISE_PAUSE);
}

/* Program MAC speed/duplex from the negotiated link partner abilities */
static void enet_apply_link(struct enet_context *context)
{
	uint32_t common = phy_read(context, MII_ADVERTISE) & phy_read(context, MII_LPA);
	uint32_t mode = StnBase;
	int full = 0;

	if (common & ADVERTISE_100FULL)      { full = 1; mode |= StnSpeed100; }
	else if (common & ADVERTISE_100HALF) {           mode |= StnSpeed100; }
	else if (common & ADVERTISE_10FULL)  { full = 1; mode |= StnSpeed10; }
	else if (common & ADVERTISE_10HALF)  {           mode |= StnSpeed10; }

	if (full) {
		mode |= StnFullDuplex;
		SIS_W32(TxLimit, TxLimitFull);
		SIS_W32(TxMacControl, TxMacFullDuplex);
	} else {
		SIS_W32(TxLimit, TxLimitHalf);
		SIS_W32(TxMacControl, TxMacHalfDuplex);
	}
	SIS_W32(StationControl, mode);
}

static int enet_open(struct netif *netif)
{
	int tries=10;
	struct enet_context *context = (struct enet_context *) netif->state;
	uint8_t *mac = netif->hwaddr;

	if (enet_probe(context)) {
	    printf("enet: Failed to probe phy\n");
		return 1;
	}

	SIS_W32(IntrMask, 0); // disable interrupts
        // NIC reset, values were 0x08558001 / 0x08550001 (...5508) before only the top byte differs
	SIS_W32(IntrControl, 0x04558001);
	udelay(100);
	SIS_W32(IntrControl, 0x04550001);

	SIS_W32(GMIIControl, 0x4);
	udelay(100);
	SIS_W32(GMIIControl, 0);

	// Set MTU (0x05f2 = 1522)
	SIS_W32(RxMPSControl, 0x000005f2);

	// important
	SIS_W32(TxMacControl, 0x00002360);

	// mac 1+2
	SIS_W32(RxMPSControl, ((uint32_t)mac[1] << 24) | ((uint32_t)mac[0] << 16) | 0x05f2);
	SIS_W32(rsv4, ((uint32_t)mac[5] << 24) | ((uint32_t)mac[4] << 16) | ((uint32_t)mac[3] << 8) | mac[2]);

	// RxMacControl (low 16 bits) + RxMacAddr[0..1] (high 16 bits)
	SIS_W32(RxMacControl, ((uint32_t)mac[1] << 24) | ((uint32_t)mac[0] << 16) |
		AcceptBroadcast | AcceptMulticast | AcceptMyPhys | AcceptErr | AcceptRunt | 0x08);
	// RxMacAddr[2..5]
	SIS_W32(RxMacAddr + 2, ((uint32_t)mac[5] << 24) | ((uint32_t)mac[4] << 16) | ((uint32_t)mac[3] << 8) | mac[2]);

	// multicast hash
	SIS_W32(RxHashTable, 0);
	SIS_W32(RxHashTable + 4, 0);

	SIS_W32(TxControl, 0x00001c00);
	SIS_W32(RxControl, 0x00101c00);

	SIS_W32(StationControl, 0x04001901);

	void *base = (void*)memalign(0x10000,0x10000);

	tx_init(context, (void*)MEM(base));
	rx_init(context, (void*)MEM(base + 0x8000));

	SIS_W32(StationControl, 0x04001001);

#if 1
	printf("Reinit PHY...\n");
	// get PHY out of reset state
	xenon_gpio_control(0,0,0x10);
	xenon_gpio_control(4,0,0x10);

	phy_write(context, MII_BMCR, BMCR_RESET | BMCR_ANENABLE);
	for (tries = 1000; tries > 0; tries--)
	{
		if (!phy_read(context, MII_BMCR) & BMCR_RESET)
			break;
		udelay(500);
	}

	enet_phy_setup(context);

	if (!phy_link_up(context))
	{
		tries=10;
		printf("Waiting for link...");
		while (!phy_link_up(context)){
			mdelay(500);
			tries--;
			if (tries<=0) break;
		};
	}

	if (phy_link_up(context)) {
		enet_apply_link(context);
		printf("link up!\n");
		netif_set_link_up(netif);
	} else {
		printf("link still down.\n");
		netif_set_link_down(netif);
	}
#endif

	/* reset released, same value as in the NicReset sequence above */
	SIS_W32(IntrControl, 0x04550001);

	SIS_W32(RxControl, 0x00101c01 | CmdReset); // enable RX
	SIS_W32(TxControl, 0x00001c00 | CmdTxEnb);  // enable TX

	return 0;
}

static struct pbuf *enet_linkinput(struct enet_context *context)
{
	volatile struct RxDesc *d = context->rx_descriptor_base + context->rx_descriptor_rptr;

	memdcbf((void*)d, sizeof(struct RxDesc));

	if (__builtin_bswap32(d->status) & OWNbit) /* check ownership */
	{
	    // printf("no data!\n");
		return 0;
	}

	// printf("RX descriptor %d contains data!\n", context->rx_descriptor_rptr);
	// printf("%08x %08x %08x %08x\n",
	//    __builtin_bswap32(d[0]),
	//    __builtin_bswap32(d[1]),
	//    __builtin_bswap32(d[2]),
	//    __builtin_bswap32(d[3]));

	int size = __builtin_bswap32(d->PSize) & RxSizeMask;
	void *phys_addr = context->rx_receive_base + context->rx_descriptor_rptr * MTU;

	memdcbf(phys_addr, size);

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
	d->addr = __builtin_bswap32(virt_to_phys(context->rx_receive_base + context->rx_descriptor_rptr * MTU));
	d->size = __builtin_bswap32(MTU | ((context->rx_descriptor_rptr == RX_DESCRIPTOR_NUM - 1) ? RingEnd : 0));
	d->PSize = __builtin_bswap32(0);
	d->status = __builtin_bswap32(OWNbit | INTbit);

	memdcbst((void*)d, sizeof(struct RxDesc));

	context->rx_descriptor_rptr++;
	context->rx_descriptor_rptr %= RX_DESCRIPTOR_NUM;

	return p;
}

static err_t enet_linkoutput(struct netif *netif, struct pbuf *p)
{
	struct pbuf *q;
	struct enet_context *context = (struct enet_context *) netif->state;

	// printf("enet linkoutput\n");

	void *dstptr = context->tx_buffer_base + context->tx_descriptor_wptr * MTU;
	int offset = 0;

	for(q = p; q != NULL; q = q->next) {
		memcpy(dstptr + offset, q->payload, q->len);
		offset += q->len;
	}

	memdcbst((void*)dstptr, p->tot_len);
	tx_data(context, dstptr, p->tot_len);

	// printf("ok, data transmitted!\n");

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
    // printf("\r%08x", mfdec());
	enet_input(netif);
}

void enet_quiesce(void)
{
	SIS_W32(TxControl, 0);
	SIS_W32(RxControl, 0);
}
