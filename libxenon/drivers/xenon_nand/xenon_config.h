/*
 * xenon_config.h
 *
 *  Created on: Mar 4, 2011
 */

#ifndef XENON_CONFIG_H_
#define XENON_CONFIG_H_

#include <xetypes.h>

struct XCONFIG_POWER_MODE // 0x2
{
	u8 VIDDelta; 			// +0x0(0x1)
	u8 Reserved; 			// +0x1(0x1)
};

struct XCONFIG_POWER_VCS_CONTROL // 0x2
{
	u16 Configured; 		// +0x0(0x2)
	u16 Reserved; 			// +0x0(0x2)
	u16 Full; 				// +0x0(0x2)
	u16 Quiet; 				// +0x0(0x2)
    u16 Fuse; 				// +0x0(0x2)
};

#define PEERPRESSURE_INSTALLED_MAGIC 'XBPP'

struct XCONFIG_STATIC_SETTINGS
{
	u32 CheckSum;     // +0x0(0x4)
	u32 Version;      // +0x4(0x4)
	u8 Unknown[5];    // +0x8(0x5)
	u8 SMCBlock[256]; // +0xE(0x100)
	u8 Padding[2];    // +0x10E(0x2)

	// below is unofficial, used by the Peer Pressure softmod
	u32 PeerPressureMagic;        //+0x110(0x4)
	u32 PeerPressureVersion;      //+0x114(0x4)
	u32 DmaPayloadLogicalAddress; //+0x118(0x4)
	u32 SoftmodFlags;             //+0x11C(0x4)

	u8 ReservedRegion[0xE0]; // +0x120(0xE0)
};

struct XCONFIG_SECURED_SETTINGS // 0x200
{
	u32 CheckSum; 			// +0x0(0x4)
	u32 Version; 			// +0x4(0x4)
	u8 OnlineNetworkID[0x4]; // +0x8(0x4)
	u8 Reserved1[0x8]; 		// +0xc(0x8)
	u8 Reserved2[0xc]; 		// +0x14(0xc)
	u8 MACAddress[0x6]; 	// +0x20(0x6)
	u8 Reserved3[0x2]; 		// +0x26(0x2)
	u32 AVRegion; 			// +0x28(0x4)
	u16 GameRegion; 		// +0x2c(0x2)
    u8 Reserved4[0x6]; 		// +0x2e(0x6)
    u32 DVDRegion; 			// +0x34(0x4)
    u32 ResetKey; 			// +0x38(0x4)
    u32 SystemFlags; 		// +0x3c(0x4)
    struct XCONFIG_POWER_MODE PowerMode; // +0x40(0x2)
    struct XCONFIG_POWER_VCS_CONTROL PowerVcsControl; // +0x42(0x2)
    u8 ReservedRegion[0x1bc]; // +0x44(0x1bc)
};

#define AVREGION_NTSCM   1
#define AVREGION_NTSCJ   2
#define AVREGION_PAL50   3
#define AVREGION_PAL60   4
#define AVREGION_INVALID 0

#define PHISON_STATIC_CONFIG_ADDR 0x2FFC200

void xenon_config_init(void);
int xenon_config_get_avregion(void);
void xenon_config_get_mac_addr(unsigned char *hwaddr);
int xenon_config_get_vid_delta();
int xenon_config_is_softmodded();

#endif /* XENON_CONFIG_H_ */
