/*
 * xb360.h
 *
 *  Created on: Sep 4, 2008
 */

#ifndef __XB360_H
#define __XB360_H

#define XEKEY_MANUFACTURING_MODE 					0x00
#define XEKEY_ALTERNATE_KEY_VAULT 					0x01
#define XEKEY_RESERVED_BYTE2						0x02
#define XEKEY_RESERVED_BYTE3						0x03
#define XEKEY_RESERVED_WORD1						0x04
#define XEKEY_RESERVED_WORD2						0x05
#define XEKEY_RESTRICTED_HVEXT_LOADER				0x06
#define XEKEY_RESERVED_DWORD2						0x07
#define XEKEY_RESERVED_DWORD3						0x08
#define XEKEY_RESERVED_DWORD4						0x09
#define XEKEY_RESTRICTED_PRIVILEDGES				0x0A
#define XEKEY_RESERVED_QWORD2						0x0B
#define XEKEY_RESERVED_QWORD3						0x0C
#define XEKEY_RESERVED_QWORD4						0x0D
#define XEKEY_RESERVED_KEY1							0x0E
#define XEKEY_RESERVED_KEY2							0x0F
#define XEKEY_RESERVED_KEY3							0x10
#define XEKEY_RESERVED_KEY4							0x11
#define XEKEY_RESERVED_RANDOM_KEY1					0x12
#define XEKEY_RESERVED_RANDOM_KEY2					0x13
#define XEKEY_CONSOLE_SERIAL_NUMBER					0x14
#define XEKEY_MOBO_SERIAL_NUMBER					0x15
#define XEKEY_GAME_REGION							0x16
#define XEKEY_CONSOLE_OBFUSCATION_KEY				0x17
#define XEKEY_KEY_OBFUSCATION_KEY					0x18
#define XEKEY_ROAMABLE_OBFUSCATION_KEY				0x19
#define XEKEY_DVD_KEY								0x1A
#define XEKEY_PRIMARY_ACTIVATION_KEY				0x1B
#define XEKEY_SECONDARY_ACTIVATION_KEY				0x1C
#define XEKEY_GLOBAL_DEVICE_2DES_KEY1				0x1D
#define XEKEY_GLOBAL_DEVICE_2DES_KEY2				0x1E
#define XEKEY_WIRELESS_CONTROLLER_MS_2DES_KEY1		0x1F
#define XEKEY_WIRELESS_CONTROLLER_MS_2DES_KEY2 		0x20
#define XEKEY_WIRED_WEBCAM_MS_2DES_KEY1				0x21
#define XEKEY_WIRED_WEBCAM_MS_2DES_KEY2				0x22
#define XEKEY_WIRED_CONTROLLER_MS_2DES_KEY1			0x23
#define XEKEY_WIRED_CONTROLLER_MS_2DES_KEY2			0x24
#define XEKEY_MEMORY_UNIT_MS_2DES_KEY1				0x25
#define XEKEY_MEMORY_UNIT_MS_2DES_KEY2				0x26
#define XEKEY_OTHER_XSM3_DEVICE_MS_2DES_KEY1		0x27
#define XEKEY_OTHER_XSM3_DEVICE_MS_2DES_KEY2		0x28
#define XEKEY_WIRELESS_CONTROLLER_3P_2DES_KEY1		0x29
#define XEKEY_WIRELESS_CONTROLLER_3P_2DES_KEY2		0x2A
#define XEKEY_WIRED_WEBCAM_3P_2DES_KEY1				0x2B
#define XEKEY_WIRED_WEBCAM_3P_2DES_KEY2				0x2C
#define XEKEY_WIRED_CONTROLLER_3P_2DES_KEY1			0x2D
#define XEKEY_WIRED_CONTROLLER_3P_2DES_KEY2			0x2E
#define XEKEY_MEMORY_UNIT_3P_2DES_KEY1				0x2F
#define XEKEY_MEMORY_UNIT_3P_2DES_KEY2				0x30
#define XEKEY_OTHER_XSM3_DEVICE_3P_2DES_KEY1		0x31
#define XEKEY_OTHER_XSM3_DEVICE_3P_2DES_KEY2		0x32
#define XEKEY_CONSOLE_PRIVATE_KEY					0x33
#define XEKEY_XEIKA_PRIVATE_KEY						0x34
#define XEKEY_CARDEA_PRIVATE_KEY					0x35
#define XEKEY_CONSOLE_CERTIFICATE					0x36
#define XEKEY_XEIKA_CERTIFICATE						0x37
#define XEKEY_CARDEA_CERTIFICATE					0x38

unsigned int xenon_get_kv_size();
unsigned int xenon_get_kv_offset();

#define KV_FLASH_SIZE             xenon_get_kv_size()
#define KV_FLASH_OFFSET			  xenon_get_kv_offset()
#define KV_FLASH_PAGES            KV_FLASH_SIZE / 0x200
#define KV_FLASH_PTR              0x6C
#define VFUSES_SIZE               0x60
#define VFUSES_OFFSET			  0x95000

#define XELL_SIZE (256*1024)
#define XELL_FOOTER_OFFSET (256*1024-16)
#define XELL_FOOTER_LENGTH 16
#define XELL_FOOTER "xxxxxxxxxxxxxxxx"

#define XELL_OFFSET_COUNT         8
static const unsigned int xelloffsets[XELL_OFFSET_COUNT] = {0x70000, // ggBoot main xell-gggggg
															0x95060,   // FreeBOOT Single-NAND main xell-2f
															0x100000,  // XeLL-Only Image (Main)
															0xC0000,   // XeLL-Only Image (Backup)
															0xE0000,
															0xF0000,   // DevGL/Glitch2m xell-gggggg in flashfs (16mb)
															0xF4000,   // DevGL xell-gggggg in flashfs (64mb)
															0xB80000}; // DevGL/Glitch2m xell-gggggg in flashfs (BB 256/512mb)

#define REV_XENON 0
#define REV_ZEPHYR 1
#define REV_FALCON 2
#define REV_JASPER 3
#define REV_TRINITY 4
#define REV_CORONA 5
#define REV_CORONA_PHISON 6
#define REV_WINCHESTER 7
#define REV_WINCHESTER_MMC 8
#define REV_UNKNOWN (-1)

typedef struct kventry {
  char id;
  int offset;
  int length;
} kventry;


void print_key(char *name, unsigned char *data);
int cpu_get_key(unsigned char *data);
int get_virtual_cpukey(unsigned char *data);
int kv_read(unsigned char *data, int virtualcpukey);
int kv_get_dvd_key(unsigned char *dvd_key);
int kv_get_key(unsigned char keyid, unsigned char *keybuf, int *keybuflen, unsigned char *keyvault);
void print_cpu_dvd_keys(void);
//int updateXeLL(void * addr, unsigned len);
int updateXeLL(char *path);
unsigned int xenon_get_DVE();
unsigned int xenon_get_PCIBridgeRevisionID();
unsigned int xenon_get_CPU_PVR();
unsigned int xenon_get_XenosID();
int xenon_get_console_type(void);

int xenon_get_logical_nand_data(void* buf, unsigned int offset, unsigned int len);
int xenon_logical_nand_data_ok();
#define MMC_FLASH_SIZE 0x3000000

#endif /* XB360_H_ */
