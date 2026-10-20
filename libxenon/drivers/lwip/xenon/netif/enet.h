/*
 * Xbox 360 on-chip ethernet (SiS190-style MAC + external MII PHY).
 * Register/bit names follow linux's sis190 driver
 */
#ifndef __ENET_H
#define __ENET_H

#include <stdint.h>
#include "lwip/err.h"

struct netif;

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
	GIoCR           = 0x48, /* GMAC IO compensation */
	TxMacControl    = 0x50,
	TxLimit         = 0x54, /* Tx MAC timer/try limit */
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

#define PHY_MAX_ADDR    32

/* Standard MII definitions */
#define MII_BMCR        0x00    /* Basic mode control register */
#define MII_BMSR        0x01    /* Basic mode status register  */
#define MII_PHYSID1		0x02	/* PHYS ID 1                   */
#define MII_PHYSID2		0x03	/* PHYS ID 2                   */

/* Basic mode control register */
#define BMCR_RESET      0x8000	/* Reset to default state      */
#define BMCR_ANENABLE   0x1000  /* Enable auto negotiation     */

/* Basic mode status register. */
#define BMSR_LSTATUS    0x0004	/* Link status                 */

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

/* StationControl (0x40) mode bits */
#define StnSpeed10      0x00000400
#define StnSpeed100     0x00000800
#define StnFullDuplex   0x00001000
#define StnBase         0x04000001

/* TxMacControl (0x50) values: full duplex / half duplex */
#define TxMacFullDuplex 0x00002360
#define TxMacHalfDuplex 0x00003f64
#define TxLimitFull     0x00000000
#define TxLimitHalf     0x10000000

/* MII registers used for link negotiation */
#define MII_ADVERTISE   0x04
#define MII_LPA         0x05
#define ADVERTISE_10HALF   0x0020
#define ADVERTISE_10FULL   0x0040
#define ADVERTISE_100HALF  0x0080
#define ADVERTISE_100FULL  0x0100
#define ADVERTISE_PAUSE    0x0400
#define BMSR_ANEGCOMPLETE  0x0020

/* Atheros AR803x debug register window (linux qcom/qcom.h) */
#define AT803X_DEBUG_ADDR          0x1d
#define AT803X_DEBUG_DATA          0x1e
#define AT803X_DEBUG_HIB_CTRL      0x0b
#define AT803X_DEBUG_PS_HIB_EN     0x8000   /* power-saving hibernation */

/* ICS1893BF vendor registers (IDT datasheet rev F ch. 7); regs 20-31 are "reserved by IDT" */
#define ICS1893_EXTCTRL       0x10     /* register 16: Extended Control */
#define ICS1893_CMD_OVERRIDE  0x8000   /* 16.15: next MDIO write may alter command-override bits (self-clearing) */
#define ICS1893_NRZI          0x0008   /* 16.3: NRZI encoding (default 1) */
#define ICS1893_PHYADDR_SHIFT 6        /* 16.10:6: PHY address, read-only (latched from pins) */
#define ICS1893_RSVD_20       0x14     /* undocumented; xboxkrnl writes 0x4048 */
#define ICS1893_RSVD_21       0x15     /* undocumented; xboxkrnl writes 0xa000 */

/* PHY MII ids (PHYSID1) seen in xboxkrnl PhyInit */
#define PHYID_ICS       0x0015  /* ICS-1893BFLF */
#define PHYID_MICREL    0x0022  /* Micrel/Kendin OUI (KSZ80xx) */
#define PHYID_ATHEROS   0x004d  /* Atheros OUI (AR803x), qcom/at803x.c */
#define PHYID_BCM_PHAT  0x0143  /* phat: Broadcom AC131 */
#define PHYID_MRV_SLIM  0x0141  /* >= Corona: Marvell 88E3015 */

/* public interface (lwip netif driver) */
err_t enet_init(struct netif *netif);
void enet_poll(struct netif *netif);
void enet_quiesce(void);

#endif
