#ifndef H_ATA
#define H_ATA

#include <xetypes.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define XENON_ATA_REG_DATA      0
#define XENON_ATA_REG_ERROR     1
#define XENON_ATA_REG_FEATURES  1
#define XENON_ATA_REG_SECTORS   2
#define XENON_ATA_REG_SECTNUM   3
#define XENON_ATA_REG_CYLLSB    4
#define XENON_ATA_REG_CYLMSB    5
#define XENON_ATA_REG_LBALOW    3
#define XENON_ATA_REG_LBAMID    4
#define XENON_ATA_REG_LBAHIGH   5
#define XENON_ATA_REG_DISK      6
#define XENON_ATA_REG_CMD       7
#define XENON_ATA_REG_STATUS    7

#define XENON_ATA_REG2_CONTROL   0

#include "disc_io.h"

	enum xenon_ata_commands {
		XENON_ATA_CMD_READ_SECTORS = 0x20,
		XENON_ATA_CMD_READ_SECTORS_EXT = 0x24,
		XENON_ATA_CMD_READ_DMA_EXT = 0x25,
		XENON_ATA_CMD_WRITE_SECTORS = 0x30,
		XENON_ATA_CMD_WRITE_SECTORS_EXT = 0x34,
		XENON_ATA_CMD_IDENTIFY_DEVICE = 0xEC,
		XENON_ATA_CMD_IDENTIFY_PACKET_DEVICE = 0xA1,
		XENON_ATA_CMD_PACKET = 0xA0,
		XENON_ATA_CMD_SET_FEATURES = 0xEF,
	};

	enum {
		XENON_ATA_CHS = 0,
		XENON_ATA_LBA,
		XENON_ATA_LBA48
	};

#define XENON_DISK_SECTOR_SIZE 0x200
#define XENON_CDROM_SECTOR_SIZE 2048

	enum {
		XENON_ATA_DMA_TABLE_OFS = 4,
		XENON_ATA_DMA_STATUS = 2,
		XENON_ATA_DMA_CMD = 0,
		XENON_ATA_DMA_WR = (1 << 3),
		XENON_ATA_DMA_START = (1 << 0),
		XENON_ATA_DMA_INTR = (1 << 2),
		XENON_ATA_DMA_ERR = (1 << 1),
		XENON_ATA_DMA_ACTIVE = (1 << 0),
	};

#define MAX_PRDS 16

	struct xenon_ata_dma_prd {
		uint32_t address;
		uint32_t size_flags;
	} __attribute__((packed));

	struct xenon_ata_device {
		uint32_t ioaddress;
		uint32_t ioaddress2;

		int atapi;

		int addressing_mode;

		uint16_t cylinders;
		uint16_t heads;
		uint16_t sectors_per_track;

		uint32_t size;
		struct bdev *bdev;

		struct xenon_ata_dma_prd * prds;

		char model[0x30];
	};

	struct xenon_atapi_read {
		uint8_t code;
		uint8_t reserved1;
		uint32_t lba;
		uint8_t reserved2;
		uint16_t length;
		uint8_t reserved3[3];
	} __attribute__((packed));

	int xenon_ata_init();
	int xenon_atapi_init();
	void xenon_atapi_set_modeb(void);
	int xenon_atapi_get_dvd_key_tsh943a(unsigned char *dvdkey);
	int xenon_atapi_set_dvd_key(unsigned char *dvdkey);
	
	extern struct xenon_ata_device ata;
	extern struct xenon_ata_device atapi;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
