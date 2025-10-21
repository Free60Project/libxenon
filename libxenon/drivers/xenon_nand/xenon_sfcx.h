#ifndef __xenon_sfcx_h
#define __xenon_sfcx_h

#include <xetypes.h>

//Registers
#define SFCX_CONFIG				0x00
#define SFCX_STATUS 			0x04
#define SFCX_COMMAND			0x08
#define SFCX_ADDRESS			0x0C
#define SFCX_DATA				0x10
#define SFCX_LOGICAL 			0x14
#define SFCX_PHYSICAL			0x18
#define SFCX_DPHYSADDR			0x1C
#define SFCX_MPHYSADDR			0x20
#define SFCX_PHISON				0xFC

//Commands for Command Register
#define PAGE_BUF_TO_REG			0x00 			//Read page buffer to data register
#define REG_TO_PAGE_BUF			0x01 			//Write data register to page buffer
#define LOG_PAGE_TO_BUF			0x02 			//Read logical page into page buffer
#define PHY_PAGE_TO_BUF			0x03 			//Read physical page into page buffer
#define WRITE_PAGE_TO_PHY		0x04 			//Write page buffer to physical page
#define BLOCK_ERASE				0x05 			//Block Erase
#define DMA_LOG_TO_RAM			0x06 			//DMA logical flash to main memory
#define DMA_PHY_TO_RAM			0x07 			//DMA physical flash to main memory
#define DMA_RAM_TO_PHY			0x08 			//DMA main memory to physical flash
#define UNLOCK_CMD_0			0x55 			//Unlock command 0
#define UNLOCK_CMD_1			0xAA 			//Unlock command 1

//Config Register bitmasks
#define CONFIG_DBG_MUX_SEL  	0x7C000000u		//Debug MUX Select
#define CONFIG_DIS_EXT_ER   	0x2000000u		//Disable External Error (Pre Jasper?)
#define CONFIG_CSR_DLY      	0x1FE0000u		//Chip Select to Timing Delay
#define CONFIG_ULT_DLY      	0x1F800u		//Unlocking Timing Delay
#define CONFIG_BYPASS       	0x400u			//Debug Bypass
#define CONFIG_DMA_LEN      	0x3C0u			//DMA Length in Pages
#define CONFIG_FLSH_SIZE    	0x30u			//Flash Size (Pre Jasper)
#define CONFIG_WP_EN        	0x8u			//Write Protect Enable
#define CONFIG_INT_EN       	0x4u			//Interrupt Enable
#define CONFIG_ECC_DIS      	0x2u			//ECC Decode Disable
#define CONFIG_SW_RST       	0x1u			//Software reset

//Status Register bitmasks
#define STATUS_ILL_LOG      	0x800u			//Illegal Logical Access
#define STATUS_PIN_WP_N     	0x400u			//NAND Not Write Protected
#define STATUS_PIN_BY_N     	0x200u			//NAND Not Busy
#define STATUS_INT_CP       	0x100u			//Interrupt
#define STATUS_ADDR_ER      	0x80u			//Address Alignment Error
#define STATUS_BB_ER        	0x40u			//Bad Block Error
#define STATUS_RNP_ER       	0x20u			//Logical Replacement not found
#define STATUS_ECC_ER       	0x1Cu			//ECC Error, 3 bits, need to determine each
#define STATUS_WR_ER        	0x2u			//Write or Erase Error
#define STATUS_BUSY         	0x1u			//Busy
#define STATUS_ERROR			(STATUS_ILL_LOG|STATUS_ADDR_ER|STATUS_BB_ER|STATUS_RNP_ER|STATUS_ECC_ER|STATUS_WR_ER)

//Page bitmasks
#define PAGE_VALID          	0x4000000u
#define PAGE_PID            	0x3FFFE00u

// API Consumers should use these two defines to
// use for creating static buffers at compile time
#define MAX_PAGE_SZ 			0x210			//Max known hardware physical page size
#define MAX_BLOCK_SZ 			0x42000 		//Max known hardware physical block size

#define META_TYPE_0				0x00 			//Pre Jasper
#define META_TYPE_1				0x01 			//Jasper 16MB
#define META_TYPE_2				0x02			//Jasper 256MB and 512MB Large Block

#define CONFIG_BLOCKS			0x04			//Number of blocks assigned for config data

// variables should NOT be in a header file! anything that uses this header is increased in size by this amount! [cOz]
// static unsigned char sfcx_page[MAX_PAGE_SZ];   //Max known hardware physical page size
// static unsigned char sfcx_block[MAX_BLOCK_SZ]; //Max known hardware physical block size

#define RAW_NAND_64				0x4200000

#define SFCX_INITIALIZED		1

// status ok or status ecc corrected
//#define SFCX_SUCCESS(status) (((int) status == STATUS_PIN_BY_N) || ((int) status & STATUS_ECC_ER))
// define success as no ecc error and no bad block error
#define SFCX_SUCCESS(status) ((status&STATUS_ERROR)==0)

struct sfc
{
	int initialized;
	int meta_type;

	int page_sz;
	int meta_sz;
	int page_sz_phys;

	int pages_in_block;
	int block_sz;
	int block_sz_phys;

	int size_mb;
	int size_bytes;
	int size_bytes_phys;
	int size_pages;
	int size_blocks;

	int blocks_per_lg_block;
	int size_usable_fs;
	int addr_config;
	int len_config;
};

unsigned int sfcx_init(void);

void sfcx_writereg(int addr, unsigned long data);
unsigned long sfcx_readreg(int addr);

int sfcx_erase_block(int address);
int sfcx_read_page(unsigned char *data, int address, int raw);
int sfcx_write_page(unsigned char *data, int address);
int sfcx_read_block(unsigned char *data, int address, int raw);
int sfcx_write_block(unsigned char *data, int address);

void sfcx_calcecc(unsigned int *data);
int sfcx_get_blocknumber(unsigned char *data);
void sfcx_set_blocknumber(unsigned char *data, int num);
int sfcx_get_blockversion(unsigned char *data);
void sfcx_set_blockversion(unsigned char *data, int ver);
void sfcx_set_pagevalid(unsigned char *data);
void sfcx_set_pageinvalid(unsigned char *data);
int sfcx_is_pagevalid(unsigned char *data);
int sfcx_is_pagezeroed(unsigned char *data);
int sfcx_is_pageerased(unsigned char *data);
int sfcx_block_to_address(int block);
int sfcx_address_to_block(int address);
int sfcx_block_to_rawaddress(int block);
int sfcx_rawaddress_to_block(int address);
int rawflash_writeImage(int len, int f);
int try_rawflash(char *filename);
int try_rawflash_internal(char *filename, bool ignoreMetadataCheck);

int sfcx_read_metadata_type(void);

// for use in other files
extern struct sfc sfc;
#endif
