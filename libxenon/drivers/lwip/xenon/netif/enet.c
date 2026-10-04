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

#define TX_DESCRIPTOR_NUM 0x10
#define RX_DESCRIPTOR_NUM 0x10

#define MTU 1528

#define MEM(x) (0x80000000|(long)(x))

/*
 * MMIO access. The MAC is little-endian (same as SiS190), so every access
 * is byte-swapped. Values passed to SIS_W32 / returned by SIS_R32 are the
 * logical register values, directly comparable to sis190.c.
 */
#define ENET_MMIO 0xea001400

#define SIS_W32(reg, val) write32n(ENET_MMIO + (reg), __builtin_bswap32(val))
#define SIS_R32(reg)      __builtin_bswap32(read32n(ENET_MMIO + (reg)))

enum sis190_registers {
	TxControl       = 0x00,
	TxDescStartAddr = 0x04,
	TxSts           = 0x0c,
	RxControl       = 0x10,
	RxDescStartAddr = 0x14,
	IntrMask        = 0x24,
	IntrControl     = 0x28,
	StationControl  = 0x40,
	GMIIControl     = 0x44,
	TxMacControl    = 0x50,
	RxMacControl    = 0x60, /* 16 bit, followed by RxMacAddr */
	RxMacAddr       = 0x62,
	RxHashTable     = 0x68,
	RxMPSControl    = 0x78,
	rsv4            = 0x7c, /* "reserved" in sis190, holds MAC bytes here */
};

enum sis190_register_content {
	/* {Rx/Tx}CmdBits */
	CmdReset        = 0x10, /* used as the TX kick */
	CmdTxEnb        = 0x01,

	/* RxMacControl */
	AcceptErr       = 0x20,
	AcceptRunt      = 0x10,
	AcceptBroadcast = 0x0800,
	AcceptMulticast = 0x0400,
	AcceptMyPhys    = 0x0200,
};

/* Enhanced PHY access register bit definitions */
#define EhnMIIread      0x0000
#define EhnMIIwrite     0x0020
#define EhnMIIdataShift 16
#define EhnMIIpmdShift  6
#define EhnMIIregShift  11
#define EhnMIIreq       0x0010
#define EhnMIInotDone   0x0010

/* Standard MII definitions */
#define MII_BMCR        0x00
#define MII_BMSR        0x01
#define BMCR_RESET      0x8000
#define BMCR_ANENABLE   0x1000
#define BMSR_LSTATUS    0x0004

/* Descriptors (TxDesc / RxDesc, little-endian) */
struct TxDesc {
	uint32_t PSize;
	uint32_t status;
	uint32_t addr;
	uint32_t size;
};

struct RxDesc {
	uint32_t PSize;
	uint32_t status;
	uint32_t addr;
	uint32_t size;
};

enum _DescStatusBit {
	/* _Desc.status */
	OWNbit      = 0x80000000,
	INTbit      = 0x40000000,
	CRCbit      = 0x00020000,
	PADbit      = 0x00010000,
	/* _Desc.size */
	RingEnd     = 0x80000000,
	/* TxDesc.status */
	DEFEN       = 0x00200000,
	/* RxDesc.PSize */
	RxSizeMask  = 0x0000ffff,
};

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
};

static int enet_open(struct netif *ctx);
static struct pbuf *enet_linkinput(struct enet_context *context);
static err_t enet_linkoutput(struct netif *netif, struct pbuf *p);

/* PHY sits at address 1 */
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

static int phy_link_up(struct enet_context * context)
{
    phy_read(context, MII_BMSR);
    return ((phy_read(context, MII_BMSR) & BMSR_LSTATUS) != 0);
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

static int enet_open(struct netif *netif)
{
	int tries=10;
	struct enet_context *context = (struct enet_context *) netif->state;
	uint8_t *mac = netif->hwaddr;

	context->phy_id = 1;
	SIS_W32(IntrMask, 0); // disable interrupts
	SIS_W32(IntrControl, 0x08558001);
	udelay(100);
	SIS_W32(IntrControl, 0x08550001);

	SIS_W32(GMIIControl, 0x4);
	udelay(100);
	SIS_W32(GMIIControl, 0);
	// printf("1478 before: %08x\n", read32n(0xea001478));
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

	// printf("init tx\n");
	tx_init(context, (void*)MEM(base));
	// printf("init rx\n");
	rx_init(context, (void*)MEM(base + 0x8000));

	SIS_W32(StationControl, 0x04001001);

#if 1
	printf("Reinit PHY...\n");
	// get PHY out of reset state
	xenon_gpio_control(0,0,0x10);
	xenon_gpio_control(4,0,0x10);

	phy_write(context, MII_BMCR, BMCR_RESET | BMCR_ANENABLE);
	while (phy_read(context, MII_BMCR) & BMCR_RESET){
		mdelay(500);
		tries--;
		if (tries<=0) break;
	};

    // phy_write(context, 0x10, 0x8058);
    // phy_write(context, 4, 0x5e1);
    // phy_write(context, 0x14, 0x4048);

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
		printf("link up!\n");
		netif_set_link_up(netif);
	} else {
		printf("link still down.\n");
		netif_set_link_down(netif);
	}
#endif

	SIS_W32(IntrControl, 0x08550001);

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
