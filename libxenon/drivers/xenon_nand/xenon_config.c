/*
 * xenon_config.c
 *
 *  Created on: Mar 4, 2011
 */

#include <xetypes.h>
#include <stdio.h>
#include <string.h>
#include "xenon_config.h"
#include "xenon_sfcx.h"
#include "xb360/xb360.h"

#define BLOCK_OFFSET 3

extern struct sfc sfc;
struct XCONFIG_STATIC_SETTINGS static_settings = {0};
struct XCONFIG_SECURED_SETTINGS secured_settings = {0};

static int xenon_config_initialized=0;
unsigned char pagebuf[MAX_PAGE_SZ];   //Max known hardware physical page size

void xenon_config_init(void)
{
	if (xenon_config_initialized) 
		return;

	uint32_t addr = 0;
	
	// load secured config (MAC, AVregion etc)
	if (xenon_get_console_type() == REV_CORONA_PHISON || xenon_get_console_type() == REV_WINCHESTER_MMC)
		addr = PHISON_STATIC_CONFIG_ADDR;
	else
	{
		sfcx_init();
		if(sfc.initialized != SFCX_INITIALIZED)
		{
			printf(" ! config: sfcx not initialized\n"); //Incompatible model found?!
			return;
		}
		else
			addr = sfc.addr_config + (BLOCK_OFFSET * sfc.block_sz) + sfc.page_sz; //Get Adress based on SFC type
	}
	xenon_get_logical_nand_data(&secured_settings, addr, sizeof secured_settings);

	// load static SMC config (SMC, peerpressure)
	// TODO: make the addreess calc cleaner, but it's always before the secured settings
	addr -= 0x200;
	xenon_get_logical_nand_data(&static_settings, addr, sizeof static_settings);

	xenon_config_initialized=1;
}

int xenon_config_get_avregion(void)
{
	xenon_config_init(); //Make sure it's initalized!

	unsigned char buf[0x4] = {0x00,0x00,0x00,0x00};

	//read from nand
	memcpy(buf, &secured_settings.AVRegion, 4);

	//check if we got erased nand data
	if (buf[0x0]==0xFF && buf[0x1]==0xFF && buf[0x2]==0xFF)
		return 0;

	//check if we got zeroed nand data
	if (buf[0x0]==0x00 && buf[0x1]==0x00 && buf[0x2]==0x00)
		return 0;



	if (buf[0x2] >= 0x01 && buf[0x2] <= 0x04)
		return buf[0x2];
	else
	{
		printf(" !!! Unknown AVRegion: %02X!!!\n",buf[0x2]);
		return buf[0x2];
	}
}

void xenon_config_get_mac_addr(unsigned char *hwaddr)
{
	xenon_config_init(); //Make sure it's initalized!

	unsigned char dmac[0x6] = {0x00,0x22,0x48,0xFF,0xFF,0xFF};

	//read from nand
	memcpy(hwaddr, &secured_settings.MACAddress[0x0], 6);

	//check if we got erased nand data
	if (hwaddr[0x0]==0xFF && hwaddr[0x1]==0xFF && hwaddr[0x2]==0xFF)
		memcpy(hwaddr, dmac, 6);

	//check if we got zeroed nand data
	if (hwaddr[0x0]==0x00 && hwaddr[0x1]==0x00 && hwaddr[0x2]==0x00)
		memcpy(hwaddr, dmac, 6);

	//printf("NIC MAC set to %02X%02X%02X%02X%02X%02X\n",
	//		hwaddr[0], hwaddr[1], hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]);
}

int xenon_config_get_vid_delta()
{
	xenon_config_init(); //Make sure it's initalized!

	u8 delta=secured_settings.PowerMode.VIDDelta;
	
	//check if we got erased or zeroed nand data
	if (delta==0 || delta==0xff) return -1;
			
	return delta;
}

int xenon_config_is_softmodded()
{
	if (!xenon_config_initialized)
		return 0;

	// return if the Peer Pressure softmod is installed
	return static_settings.PeerPressureMagic == PEERPRESSURE_INSTALLED_MAGIC;
}
