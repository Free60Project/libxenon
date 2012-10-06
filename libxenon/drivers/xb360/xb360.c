/*
 * xb360.c
 *
 *  Created on: Sep 4, 2008
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pci/io.h>
#include <time/time.h>
#include <xenon_nand/xenon_sfcx.h>
#include <xenon_nand/xenon_config.h>
#include <xenon_soc/xenon_secotp.h>
#include <xenon_smc/xenon_smc.h>
#include <crypt/hmac_sha1.h>
#include <crypt/rc4.h>

#include "xb360.h"

extern struct XCONFIG_SECURED_SETTINGS secured_settings;

extern struct sfc sfc;

static const kventry kvlookup[] =
	{
	{XEKEY_MANUFACTURING_MODE, 					0x18, 	0x01},
	{XEKEY_ALTERNATE_KEY_VAULT, 				0x19, 	0x01},
	{XEKEY_RESERVED_BYTE2, 						0x1A, 	0x01},
	{XEKEY_RESERVED_BYTE3, 						0x1B, 	0x01},
	{XEKEY_RESERVED_WORD1, 						0x1C, 	0x02},
	{XEKEY_RESERVED_WORD2, 						0x1E, 	0x02},
	{XEKEY_RESTRICTED_HVEXT_LOADER, 			0x20, 	0x02},
	{XEKEY_RESERVED_DWORD2, 					0x24, 	0x04},
	{XEKEY_RESERVED_DWORD3, 					0x28, 	0x04},
	{XEKEY_RESERVED_DWORD4, 					0x2c, 	0x04},
	{XEKEY_RESTRICTED_PRIVILEDGES, 				0x30, 	0x08},
	{XEKEY_RESERVED_QWORD2, 					0x38, 	0x08},
	{XEKEY_RESERVED_QWORD3, 					0x40, 	0x08},
	{XEKEY_RESERVED_QWORD4, 					0x48, 	0x08},
	{XEKEY_RESERVED_KEY1, 						0x50,	0x10},
	{XEKEY_RESERVED_KEY2, 						0x60, 	0x10},
	{XEKEY_RESERVED_KEY3, 						0x70, 	0x10},
	{XEKEY_RESERVED_KEY4, 						0x80, 	0x10},
	{XEKEY_RESERVED_RANDOM_KEY1, 				0x90, 	0x10},
	{XEKEY_RESERVED_RANDOM_KEY2, 				0xA0, 	0x10},
	{XEKEY_CONSOLE_SERIAL_NUMBER, 				0xB0, 	0x0C},
	{XEKEY_MOBO_SERIAL_NUMBER, 					0xBC, 	0x0C},
	{XEKEY_GAME_REGION, 						0xC8, 	0x02},
	{XEKEY_CONSOLE_OBFUSCATION_KEY, 			0xD0, 	0x10},
	{XEKEY_KEY_OBFUSCATION_KEY, 				0xE0, 	0x10},
	{XEKEY_ROAMABLE_OBFUSCATION_KEY, 			0xF0, 	0x10},
	{XEKEY_DVD_KEY, 							0x100, 	0x10},
	{XEKEY_PRIMARY_ACTIVATION_KEY, 				0x110, 	0x18},
	{XEKEY_SECONDARY_ACTIVATION_KEY, 			0x128, 	0x10},
	{XEKEY_GLOBAL_DEVICE_2DES_KEY1, 			0x138, 	0x10},
	{XEKEY_GLOBAL_DEVICE_2DES_KEY2, 			0x148, 	0x10},
	{XEKEY_WIRELESS_CONTROLLER_MS_2DES_KEY1, 	0x158, 	0x10},
	{XEKEY_WIRELESS_CONTROLLER_MS_2DES_KEY2 , 	0x168, 	0x10},
	{XEKEY_WIRED_WEBCAM_MS_2DES_KEY1, 			0x178, 	0x10},
	{XEKEY_WIRED_WEBCAM_MS_2DES_KEY2, 			0x188, 	0x10},
	{XEKEY_WIRED_CONTROLLER_MS_2DES_KEY1, 		0x198, 	0x10},
	{XEKEY_WIRED_CONTROLLER_MS_2DES_KEY2, 		0x1A8, 	0x10},
	{XEKEY_MEMORY_UNIT_MS_2DES_KEY1, 			0x1B8, 	0x10},
	{XEKEY_MEMORY_UNIT_MS_2DES_KEY2, 			0x1C8, 	0x10},
	{XEKEY_OTHER_XSM3_DEVICE_MS_2DES_KEY1, 		0x1D8, 	0x10},
	{XEKEY_OTHER_XSM3_DEVICE_MS_2DES_KEY2, 		0x1E8, 	0x10},
	{XEKEY_WIRELESS_CONTROLLER_3P_2DES_KEY1, 	0x1F8, 	0x10},
	{XEKEY_WIRELESS_CONTROLLER_3P_2DES_KEY2, 	0x208, 	0x10},
	{XEKEY_WIRED_WEBCAM_3P_2DES_KEY1, 			0x218, 	0x10},
	{XEKEY_WIRED_WEBCAM_3P_2DES_KEY2, 			0x228, 	0x10},
	{XEKEY_WIRED_CONTROLLER_3P_2DES_KEY1, 		0x238, 	0x10},
	{XEKEY_WIRED_CONTROLLER_3P_2DES_KEY2, 		0x248, 	0x10},
	{XEKEY_MEMORY_UNIT_3P_2DES_KEY1, 			0x258, 	0x10},
	{XEKEY_MEMORY_UNIT_3P_2DES_KEY2, 			0x268, 	0x10},
	{XEKEY_OTHER_XSM3_DEVICE_3P_2DES_KEY1, 		0x278, 	0x10},
	{XEKEY_OTHER_XSM3_DEVICE_3P_2DES_KEY2, 		0x288, 	0x10},
	{XEKEY_CONSOLE_PRIVATE_KEY, 				0x298, 	0x1D0},
	{XEKEY_XEIKA_PRIVATE_KEY, 					0x468, 	0x390},
	{XEKEY_CARDEA_PRIVATE_KEY, 					0x7F8, 	0x1D0},
	{XEKEY_CONSOLE_CERTIFICATE, 				0x9C8, 	0x1A8},
	{XEKEY_XEIKA_CERTIFICATE, 					0xB70, 	0x1388},
	{XEKEY_CARDEA_CERTIFICATE, 					0x1EF8, 0x2108}
	};

void print_key(char *name, unsigned char *data)
{
	int i=0;
	printf("%s: ", name);
	for(i=0; i<16; i++)
		printf("%02X",data[i]);
	printf("\n");
}

int cpu_get_key(unsigned char *data)
{
	*(unsigned long long*)&data[0] = xenon_secotp_read_line(3);
	*(unsigned long long*)&data[8] = xenon_secotp_read_line(5);
	return 0;
}

int virtualfuses_read(unsigned char *data)
{
  int vfuses_offset = 0x95000; //Page (0x4A8 * 0x200)
  if (sfc.initialized != SFCX_INITIALIZED)
    return 1;

  int status = sfcx_read_page(data, vfuses_offset, 0);

  //read from nand
  if (!SFCX_SUCCESS(status))
    return 2; //failure
  else
    return 0; //success
}


int get_virtual_cpukey(unsigned char *data)
{
  int result = 0;
  unsigned char buffer[MAX_PAGE_SZ];
  result = virtualfuses_read(buffer);

  if (result!=0)
  {
     printf(" ! SFCX error while reading virtualfuses\n");
     return result;
  }

    //if we got here then it was at least able to read from nand
    //now we need to verify the data somehow
  if (buffer[0]==0xC0 && buffer[1]==0xFF && buffer[2]==0xFF && buffer[3]==0xFF)
  {
	memcpy(data,&buffer[0x20],0x10);
    	return 0;
  }
  else
	/* No Virtual Fuses were found at 0x95000*/
	return 1;
}


int kv_get_key(unsigned char keyid, unsigned char *keybuf, int *keybuflen, unsigned char *keyvault)
{
	if (keyid > 0x38)
		return 1;

	if (*keybuflen != kvlookup[keyid].length)
	{
		*keybuflen = kvlookup[keyid].length;
		return 2;
	}
	memcpy(keybuf, keyvault + kvlookup[keyid].offset, kvlookup[keyid].length);

	return 0;
}


int kv_read(unsigned char *data, int virtualcpukey)
{
	int page=0;
	int pages=0;
	int togo=0;
	int kv_offset = 0;
	int cntr = 0;

	if (sfc.initialized != SFCX_INITIALIZED)
		return 1;

	unsigned char buffer[MAX_PAGE_SZ];
	memset(buffer, '\0', sizeof(buffer));

	sfcx_read_page(buffer, 0, 0);

	kv_offset =  buffer[KV_FLASH_PTR+0]; kv_offset <<= 8;
	kv_offset |= buffer[KV_FLASH_PTR+1]; kv_offset <<= 8;
	kv_offset |= buffer[KV_FLASH_PTR+2]; kv_offset <<= 8;
	kv_offset |= buffer[KV_FLASH_PTR+3];

	//printf("kv_read: kv_offset: %d\n", kv_offset);

	page = (kv_offset / sfc.page_sz);
	pages = KV_FLASH_PAGES;
	togo = pages;

	while(togo)
	{
		sfcx_read_page((unsigned char*) &data[cntr * sfc.page_sz], page * sfc.page_sz, 0);
		page++;
		cntr++;
		togo--;
	}

	unsigned char cpu_key[0x10];
        if (virtualcpukey)
            get_virtual_cpukey(cpu_key);
        else
            cpu_get_key(cpu_key);
	//print_key("kv_read: cpu key", cpu_key);

	unsigned char hmac_key[0x10];
	memcpy(hmac_key, data, 0x10);
	//print_key("kv_read: hmac key", hmac_key);

	unsigned char rc4_key[0x10];
	memset(rc4_key, 0, 0x10);

	HMAC_SHA1(cpu_key, hmac_key, rc4_key, 0x10);
	//print_key("kv_read: rc4 key", rc4_key);

	unsigned char rc4_state[0x100];
	memset(rc4_state, 0, 0x100);

	rc4_init(rc4_state, rc4_key ,0x10);
	rc4_crypt(rc4_state, (unsigned char*) &data[0x10], KV_FLASH_SIZE - 0x10);

	//Now then do a little check to make sure it is somewhat correct
	//We check the hmac_sha1 of the data and compare that to
	//the hmac_sha1 key in the header of the keyvault
	//basically the reverse of what we did to generate the key for rc4
	unsigned char data2[KV_FLASH_SIZE];
	unsigned char out[20];
	unsigned char tmp[] = {0x07, 0x12};
	HMAC_SHA1_CTX ctx;

	//the hmac_sha1 seems destructive
	//so we make a copy of the data
	memcpy(data2, data, KV_FLASH_SIZE);

	HMAC_SHA1_Init(&ctx);
	HMAC_SHA1_UpdateKey(&ctx, (unsigned char *) cpu_key, 0x10);
	HMAC_SHA1_EndKey(&ctx);

	HMAC_SHA1_StartMessage(&ctx);

	HMAC_SHA1_UpdateMessage(&ctx, (unsigned char*) &data2[0x10], KV_FLASH_SIZE - 0x10);
	HMAC_SHA1_UpdateMessage(&ctx, (unsigned char*)   &tmp[0x00], 0x02);	//Special appendage

	HMAC_SHA1_EndMessage(out, &ctx);
	HMAC_SHA1_Done(&ctx);

	int index = 0;
    while (index < 0x10)
    {
    	if (data[index] != out[index])
    	{
    		// Hmm something is wrong, hmac is not matching
    		//printf(" ! kv_read: kv hash check failed\n");
    		return 2;
    	}
    	index += 1;
    }

	return 0;
}

int kv_get_dvd_key(unsigned char *dvd_key)
{
	unsigned char buffer[KV_FLASH_SIZE], tmp[0x10];
	int result = 0;
	int keylen = 0x10;

	result = kv_read(buffer, 0);
        if (result == 2 && get_virtual_cpukey(tmp) == 0){
            printf("! Attempting to decrypt DVDKey with Virtual CPU Key !\n");
            result = kv_read(buffer, 1);
        }
	if (result != 0){
		printf(" ! kv_get_dvd_key Failure: kv_read\n");
		if (result == 2){ //Hash failure
			printf(" !   the hash check failed probably as a result of decryption failure\n");
			printf(" !   make sure that the CORRECT key vault for this console is in flash\n");
			printf(" !   the key vault should be at offset 0x4200 for a length of 0x4200\n");
			printf(" !   in the 'raw' flash binary from THIS console\n");
		}
		return 1;
	}

	result = kv_get_key(XEKEY_DVD_KEY, dvd_key, &keylen, buffer);
	if (result != 0){
		printf(" ! kv_get_dvd_key Failure: kv_get_key %d\n", result);
		return result;
	}

	//print_key("dvd key", dvd_key);
	return 0;

}

void print_cpu_dvd_keys(void)
{
	unsigned char key[0x10];

	printf("\n");

	memset(key, '\0', sizeof(key));
	if (cpu_get_key(key)==0)
		print_key(" * your cpu key", key);
		
	memset(key, '\0',sizeof(key));
	if (get_virtual_cpukey(key)==0)
		print_key(" * your virtual cpu key", key);

	memset(key, '\0', sizeof(key));
	if (kv_get_dvd_key(key)==0)
		print_key(" * your dvd key", key);

	printf("\n");
}

int updateXeLL(void * addr, unsigned len)
{
	int i, j, k, startblock, current, offsetinblock, blockcnt;
	unsigned char *user, *spare;
    
    if (sfc.initialized != SFCX_INITIALIZED){
        printf(" ! sfcx is not initialized! Unable to update XeLL in NAND!\n");
		return -1;
    }
    
    printf("\n * found XeLL update. press power NOW if you don't want to update.\n");
    delay(15);
    
    for (k = 0; k < XELL_OFFSET_COUNT; k++)
    {
      current = xelloffsets[k];
      offsetinblock = current % sfc.block_sz;
      startblock = current/sfc.block_sz;
      blockcnt = offsetinblock ? (XELL_SIZE/sfc.block_sz)+1 : (XELL_SIZE/sfc.block_sz);
      
    
      spare = (unsigned char*)malloc(blockcnt*sfc.pages_in_block*sfc.meta_sz);
      if(!spare){
        printf(" ! Error while memallocating filebuffer (spare)\n");
        return -1;
      }
      user = (unsigned char*)malloc(blockcnt*sfc.block_sz);
      if(!user){
        printf(" ! Error while memallocating filebuffer (user)\n");
        return -1;
      }
      j = 0;
      unsigned char pagebuf[MAX_PAGE_SZ];	

      for (i = (startblock*sfc.pages_in_block); i< (startblock+blockcnt)*sfc.pages_in_block; i++)
      {
         sfcx_read_page(pagebuf, (i*sfc.page_sz), 1);
		//Split rawpage into user & spare
		memcpy(&user[j*sfc.page_sz],pagebuf,sfc.page_sz);
		memcpy(&spare[j*sfc.meta_sz],&pagebuf[sfc.page_sz],sfc.meta_sz);
		j++;
      }
      
        if (memcmp(&user[offsetinblock+(XELL_FOOTER_OFFSET)],XELL_FOOTER,XELL_FOOTER_LENGTH) == 0){
            printf(" * XeLL Binary in NAND found @ 0x%08X\n", (startblock*sfc.block_sz)+offsetinblock);
         
        memcpy(&user[offsetinblock], addr,len); //Copy over updxell.bin
        printf(" * Writing to NAND!\n");
		j = 0;
        for (i = startblock*sfc.pages_in_block; i < (startblock+blockcnt)*sfc.pages_in_block; i ++)
        {
			if (!(i%sfc.pages_in_block))
			sfcx_erase_block(i*sfc.page_sz);

			/* Copy user & spare data together in a single rawpage */
            memcpy(pagebuf,&user[j*sfc.page_sz],sfc.page_sz);
			memcpy(&pagebuf[sfc.page_sz],&spare[j*sfc.meta_sz],sfc.meta_sz);
			j++;

			if (!(sfcx_is_pageerased(pagebuf))) // We dont need to write to erased pages
			{
				memset(&pagebuf[sfc.page_sz+0x0C],0x0, 4); //zero only EDC bytes
				sfcx_calcecc((unsigned int *)pagebuf); 	  //recalc EDC bytes
				sfcx_write_page(pagebuf, i*sfc.page_sz);
			}
        }
        printf(" * XeLL flashed! Reboot the xbox to enjoy the new build\n");
		for(;;);
	
		}
	}
    printf(" ! Couldn't locate XeLL binary in NAND. Aborting!\n");
    return -1; // if this point is reached, updating xell failed
}

unsigned int xenon_get_DVE()
{
	unsigned int DVEversion, tmp;
	xenon_smc_ana_read(0xfe, &DVEversion);
    tmp = DVEversion;
    tmp = (tmp & ~0xF0) | ((DVEversion >> 12) & 0xF0);
    return (tmp & 0xFF);
}

unsigned int xenon_get_PCIBridgeRevisionID()
{
	return ((read32(0xd0000008) << 24) >> 24);
}

unsigned int xenon_get_CPU_PVR()
{
	unsigned int PVR;
	asm volatile("mfpvr %0" : "=r" (PVR));
	return PVR;
}

unsigned int xenon_get_XenosID()
{
	return ((read32(0xd0010000) >> 16) & 0xFFFF);
}

int xenon_get_console_type()
{
    unsigned int PVR, PCIBridgeRevisionID, consoleVersion, DVEversion;
    
    PCIBridgeRevisionID = xenon_get_PCIBridgeRevisionID();
    consoleVersion = xenon_get_XenosID();
    DVEversion = xenon_get_DVE();
    PVR = xenon_get_CPU_PVR();
    
    if(PVR == 0x710200 || PVR == 0x710300)
    {
        return REV_ZEPHYR;
    }
    else
    {
        if(consoleVersion < 0x5821)
        {
		return REV_XENON;
        }
        else if(consoleVersion >= 0x5821 && consoleVersion < 0x5831)
        {
		return REV_FALCON;
        }
        else if(consoleVersion >= 0x5831 && consoleVersion < 0x5841)
        {
		return REV_JASPER;
        }
        else if(consoleVersion >= 0x5841 && consoleVersion < 0x5851)
        {
			if (DVEversion >= 0x20)
				return REV_CORONA;
			else
				return REV_TRINITY;
        }
        else if(consoleVersion >= 0x5851)
        {
		return REV_WINCHESTER;
        }
    }

    return REV_UNKNOWN;
}
