/* modified to fit in libxenon */

/* ata.c - ATA disk access.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <time/time.h>
#include <ppc/cache.h>
#include <malloc.h>
#include "ata.h"

#include <debug.h>
#include <byteswap.h>

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

//No printf
//#define printf(...)

#define USE_DMA

struct xenon_ata_device ata;
struct xenon_ata_device atapi;

static inline void
xenon_ata_regset(struct xenon_ata_device *dev, int reg, uint8_t val) {
	*(volatile uint8_t*)(dev->ioaddress + reg) = val;
}

static inline uint8_t
xenon_ata_regget(struct xenon_ata_device *dev, int reg) {
	return *(volatile uint8_t*)(dev->ioaddress + reg);
}

static inline void
xenon_ata_regset2(struct xenon_ata_device *dev, int reg, int val) {
	*(volatile uint8_t*)(dev->ioaddress2 + reg) = val;
}

static inline int
xenon_ata_regget2(struct xenon_ata_device *dev, int reg) {
	return *(volatile uint8_t*)(dev->ioaddress2 + reg);
}

static inline uint32_t
xenon_ata_read_data(struct xenon_ata_device *dev, int reg) {
	return *(volatile uint32_t*)(dev->ioaddress + reg);
}

static inline void
xenon_ata_write_data(struct xenon_ata_device *dev, int reg, uint32_t val) {
	*(volatile uint32_t*)(dev->ioaddress + reg) = val;
}

static inline void
xenon_ata_write_data2(struct xenon_ata_device *dev, int reg, uint32_t val) {
	*(volatile uint32_t*)(dev->ioaddress2 + reg) = val;
}

static inline void
xenon_ata_wait_busy(struct xenon_ata_device *dev) {
	while ((xenon_ata_regget(dev, XENON_ATA_REG_STATUS) & 0x80));
}

static inline void
xenon_ata_wait_drq(struct xenon_ata_device *dev) {
	while (!(xenon_ata_regget(dev, XENON_ATA_REG_STATUS) & 0x08));
}

static inline void
xenon_ata_wait_ready(struct xenon_ata_device *dev) {
	while ((xenon_ata_regget(dev, XENON_ATA_REG_STATUS) & 0xC0) != 0x40);
}

static inline void
xenon_ata_wait() {
	mdelay(50);
}

static int
xenon_ata_pio_read(struct xenon_ata_device *dev, char *buf, int size) {
	uint32_t *buf32 = (uint32_t *) buf;
	unsigned int i;

	if (xenon_ata_regget(dev, XENON_ATA_REG_STATUS) & 1)
		return xenon_ata_regget(dev, XENON_ATA_REG_ERROR);

	/* Wait until the data is available.  */
	xenon_ata_wait_drq(dev);

	/* Read in the data, word by word.  */
	if (size & 0xf) {
		for (i = 0; i < size / 4; i++)
			buf32[i] = xenon_ata_read_data(dev, XENON_ATA_REG_DATA);
	} else {
		volatile uint32_t* reg_data = (volatile uint32_t*)(dev->ioaddress + XENON_ATA_REG_DATA);
		for (i = 0; i < size / 16; ++i) {
			*buf32++ = *reg_data;
			*buf32++ = *reg_data;
			*buf32++ = *reg_data;
			*buf32++ = *reg_data;
		}
	}

	if (xenon_ata_regget(dev, XENON_ATA_REG_STATUS) & 1)
		return xenon_ata_regget(dev, XENON_ATA_REG_ERROR);

	return 0;
}

static int
xenon_ata_pio_write(struct xenon_ata_device *dev, char *buf, int size) {
	uint32_t *buf32 = (uint32_t *) buf;
	unsigned int i;

	/* Wait until the device is ready to write.  */
	xenon_ata_wait_drq(dev);

	/* Write the data, word by word.  */
	for (i = 0; i < size / 4; i++)
		xenon_ata_write_data(dev, XENON_ATA_REG_DATA, buf32[i]);

	if (xenon_ata_regget(dev, XENON_ATA_REG_STATUS) & 1)
		return xenon_ata_regget(dev, XENON_ATA_REG_ERROR);

	return 0;
}

static int
xenon_ata_dma_read(struct xenon_ata_device *dev, char *buf, int size) {

	assert(!((uint32_t) buf & 3));

	uint32_t i, b;
	int s, ss;

	if (xenon_ata_regget(dev, XENON_ATA_REG_STATUS) & 1)
		return xenon_ata_regget(dev, XENON_ATA_REG_ERROR);

	//	printf("ATA DMA %p %d\n",buf,size);

	/* build PRDs */

	i = 0;
	s = size;
	b = ((uint32_t) buf)&0x7fffffff;
	while (s > 0) {
		dev->prds[i].address = __builtin_bswap32(b);

		ss = MIN(s, 0x10000 - (b & 0xffff)); // a PRD buffer mustn't cross a 64k boundary
		s -= ss;
		b += ss;

		dev->prds[i].size_flags = __builtin_bswap32((ss & 0xffff) | (s > 0 ? 0 : 0x80000000));

		//		printf("PRD %08x %d\n",b,ss);

		++i;
		if (i >= MAX_PRDS && s > 0) {
			printf("ATA DMA transfer too big\n");
			return -1;
		}
	}

	memdcbst(dev->prds, MAX_PRDS * sizeof (struct xenon_ata_dma_prd));

	/* setup DMA regs */

	xenon_ata_write_data2(dev, XENON_ATA_DMA_TABLE_OFS, __builtin_bswap32((uint32_t) dev->prds & 0x7fffffff));
	xenon_ata_regset2(dev, XENON_ATA_DMA_CMD, XENON_ATA_DMA_WR);
	xenon_ata_regset2(dev, XENON_ATA_DMA_STATUS, 0);

	/* flush buffer from cache */

	memdcbf(buf, size);

	/* start DMA */

	xenon_ata_regset2(dev, XENON_ATA_DMA_CMD, XENON_ATA_DMA_WR | XENON_ATA_DMA_START);

	/* wait for DMA end */

	while (xenon_ata_regget2(dev, XENON_ATA_DMA_STATUS) & XENON_ATA_DMA_ACTIVE);

	/* stop DMA ctrlr */

	xenon_ata_regset2(dev, XENON_ATA_DMA_CMD, 0);

	if (xenon_ata_regget2(dev, XENON_ATA_DMA_STATUS) & XENON_ATA_DMA_ERR) {
		printf("ATA DMA transfer error\n");
		return -1;
	}

	if (xenon_ata_regget(dev, XENON_ATA_REG_STATUS) & 1)
		return xenon_ata_regget(dev, XENON_ATA_REG_ERROR);

	/* reload buffer into cache */

	memdcbt(buf, size);

	return 0;
};

static void
xenon_ata_dumpinfo(struct xenon_ata_device *dev, char *info) {
	char text[41];
	char data[0x80];
	int i;

	for (i = 0; i<sizeof (data); i += 2) {
		data[i] = info[i + 1];
		data[i + 1] = info[i];
	}

	/* The device information was read, dump it for debugging.  */
	strncpy(text, data + 20, 20);
	text[20] = 0;
	printf("  * Serial: %s\n", text);
	strncpy(text, data + 46, 8);
	text[8] = 0;
	printf("  * Firmware: %s\n", text);
	strncpy(text, data + 54, 40);
	text[40] = 0;
	strncpy(dev->model, text, sizeof(dev->model));
	printf("  * Model: %s\n", dev->model);

	if (!dev->atapi) {
		printf("  * Addressing mode: %d\n", dev->addressing_mode);
		printf("  * #cylinders: %d\n", (int) dev->cylinders);
		printf("  * #heads: %d\n", (int) dev->heads);
		printf("  * #sectors: %d\n", (int) dev->size);
	}
}

static void
xenon_ata_setlba(struct xenon_ata_device *dev, int sector, int size) {
	xenon_ata_regset(dev, XENON_ATA_REG_SECTORS, size);
	xenon_ata_regset(dev, XENON_ATA_REG_LBALOW, sector & 0xFF);
	xenon_ata_regset(dev, XENON_ATA_REG_LBAMID, (sector >> 8) & 0xFF);
	xenon_ata_regset(dev, XENON_ATA_REG_LBAHIGH, (sector >> 16) & 0xFF);
}

static int
xenon_ata_setaddress(struct xenon_ata_device *dev, int sector, int size) {
	xenon_ata_wait_busy(dev);

	switch (dev->addressing_mode) {
		case XENON_ATA_CHS:
		{
			unsigned int cylinder;
			unsigned int head;
			unsigned int sect;

			/* Calculate the sector, cylinder and head to use.  */
			sect = ((uint32_t) sector % dev->sectors_per_track) + 1;
			cylinder = (((uint32_t) sector / dev->sectors_per_track)
					/ dev->heads);
			head = ((uint32_t) sector / dev->sectors_per_track) % dev->heads;

			if (sect > dev->sectors_per_track
					|| cylinder > dev->cylinders
					|| head > dev->heads) {
				printf("sector %d can not be addressed "
						"using CHS addressing", sector);
				return -1;
			}

			xenon_ata_regset(dev, XENON_ATA_REG_SECTNUM, sect);
			xenon_ata_regset(dev, XENON_ATA_REG_CYLLSB, cylinder & 0xFF);
			xenon_ata_regset(dev, XENON_ATA_REG_CYLMSB, cylinder >> 8);
			xenon_ata_regset(dev, XENON_ATA_REG_DISK, head);

			break;
		}

		case XENON_ATA_LBA:
			if (size == 256)
				size = 0;
			xenon_ata_setlba(dev, sector, size);
			xenon_ata_regset(dev, XENON_ATA_REG_DISK,
					0xE0 | ((sector >> 24) & 0x0F));
			break;

		case XENON_ATA_LBA48:
			if (size == 65536)
				size = 0;

			/* Set "Previous".  */
			xenon_ata_setlba(dev, sector >> 24, size >> 8);
			/* Set "Current".  */
			xenon_ata_setlba(dev, sector, size);
			xenon_ata_regset(dev, XENON_ATA_REG_DISK, 0xE0);

			break;

		default:
			return -1;
	}
	return 0;
}

static int
xenon_ata_read_sectors(sec_t start_sector, sec_t sector_size, void *buf) {
	//struct xenon_ata_device *dev = bdev->ctx;
	struct xenon_ata_device *dev = &ata;

	//start_sector += bdev->offset;

	xenon_ata_setaddress(dev, start_sector, sector_size);

	/* Read sectors.  */
#ifndef USE_DMA
	unsigned int sect = 0;
	xenon_ata_regset(dev, XENON_ATA_REG_CMD, XENON_ATA_CMD_READ_SECTORS_EXT);
	xenon_ata_wait_ready(dev);
	for (sect = 0; sect < sector_size; sect++) {
		if (xenon_ata_pio_read(dev, buf, XENON_DISK_SECTOR_SIZE)) {
			asm volatile ("sync");
			printf("ATA read error\n");
			return -1;
		}
		buf += XENON_DISK_SECTOR_SIZE;
	}
	asm volatile ("sync");
	return sect;
#else
	xenon_ata_regset(dev, XENON_ATA_REG_CMD, XENON_ATA_CMD_READ_DMA_EXT);
	xenon_ata_wait_ready(dev);

	if (xenon_ata_dma_read(dev, buf, sector_size * XENON_DISK_SECTOR_SIZE)) {
		printf("ATA DMA read error\n");
		asm volatile ("sync");
		return -1;
	}
	asm volatile ("sync");
	return sector_size;
#endif
}

static int
//xenon_ata_write_sectors(struct bdev *bdev, const void *buf, lba_t start_sector, int sector_size) {
xenon_ata_write_sectors(lba_t start_sector, int sector_size, const void *buf) {
	unsigned int sect;
	//    struct xenon_ata_device *dev = bdev->ctx;
	struct xenon_ata_device *dev = &ata;

	//start_sector += bdev->offset;

	xenon_ata_setaddress(dev, start_sector, sector_size);

	/* Read sectors.  */
	xenon_ata_regset(dev, XENON_ATA_REG_CMD, XENON_ATA_CMD_WRITE_SECTORS_EXT);
	xenon_ata_wait_ready(dev);
	for (sect = 0; sect < sector_size; sect++) {
		if (xenon_ata_pio_write(dev, (void *) buf, XENON_DISK_SECTOR_SIZE)) {
			printf("ATA write error\n");
			return -1;
		}
		buf += XENON_DISK_SECTOR_SIZE;
	}

	return sect;
}

__attribute__((unused)) static int
xenon_atapi_identify(struct xenon_ata_device *dev) {

	char info[XENON_DISK_SECTOR_SIZE];
	xenon_ata_wait_busy(dev);

	xenon_ata_regset(dev, XENON_ATA_REG_DISK, 0xE0);
	xenon_ata_regset(dev, XENON_ATA_REG_CMD,
			XENON_ATA_CMD_IDENTIFY_PACKET_DEVICE);
	xenon_ata_wait_ready(dev);

	xenon_ata_pio_read(dev, info, sizeof (info));

	dev->atapi = 1;

	xenon_ata_dumpinfo(dev, info);

	return 0;
}

static int
xenon_atapi_packet(struct xenon_ata_device *dev, char *packet, int dma) {

	xenon_ata_regset(dev, XENON_ATA_REG_DISK, 0);
	xenon_ata_regset(dev, XENON_ATA_REG_FEATURES, dma ? 1 : 0);
	xenon_ata_regset(dev, XENON_ATA_REG_SECTORS, 0);
	xenon_ata_regset(dev, XENON_ATA_REG_LBAHIGH, 0xff);
	xenon_ata_regset(dev, XENON_ATA_REG_LBAMID, 0xff);
	xenon_ata_regset(dev, XENON_ATA_REG_CMD, XENON_ATA_CMD_PACKET);
	xenon_ata_wait_ready(dev);

	xenon_ata_pio_write(dev, packet, 12);

	return 0;
}

#define   SK(sense)((sense>>16) & 0xFF)
#define  ASC(sense)((sense>>8)  & 0xFF)
#define ASCQ(sense)((sense>>0)  & 0xFF)

int
xenon_atapi_request_sense(struct xenon_ata_device *dev) {
	char cdb[12] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	char buf[24];
	memset(buf, 0, sizeof (buf));

	cdb[4] = sizeof (buf);

	xenon_atapi_packet(dev, cdb, 0);
	xenon_ata_wait_ready(dev);
	if (xenon_ata_pio_read(dev, buf, sizeof (buf))) {
		printf("ATAPI request sense failed\n");
		return -1;
	};

	return (buf[2] << 16) | (buf[12] << 8) | buf[13];
}

static int
xenon_atapi_inquiry_model(struct xenon_ata_device *dev) {

	char cdb[12] = {0x12, 0x00, 0x00, 0x00, 0x24, 0xC0,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	char buf[0x24];
	memset(buf, 0, sizeof (buf));

	xenon_atapi_packet(dev, cdb, 0);
	xenon_ata_wait_ready(dev);
	if (xenon_ata_pio_read(dev, buf, sizeof (buf))) {
		printf("ATAPI inquiry failed\n");
		return -1;
	};

	buf[8 + 24] = '\0';
	strncpy(dev->model, &buf[8], sizeof(dev->model));
	printf("ATAPI inquiry model: %s %d\n", dev->model);

	return 0;
}

void
xenon_atapi_set_modeb(void) {
	struct xenon_ata_device *dev = &atapi;

	char buf[0x30];
	memset(buf, 0, sizeof (buf));

	char cdb[12] = {0xE7, 0x48, 0x49, 0x54, 0x30, 0x90,
		0x90, 0xd0, 0x01, 0x00, 0x00, 0x00};

	xenon_atapi_packet(dev, cdb, 0);
	xenon_ata_wait_ready(dev);
	if (xenon_ata_pio_read(dev, buf, sizeof (buf))) {
		printf("DVD set mode b failed\n");
	}
}

int
xenon_atapi_get_dvd_key_tsh943a(unsigned char *dvdkey) {
	struct xenon_ata_device *dev = &atapi;

	// create mode page buffer
	char buf[0x10];
	memset(buf, 0, sizeof (buf));

	char cdb[12] = {0xFF, 0x08, 0x05, 0x00, 0x05, 0x01,
		0x03, 0x00, 0x04, 0x07, 0x00, 0x00};

	xenon_atapi_packet(dev, cdb, 0);
	xenon_ata_wait_ready(dev);
	if (xenon_ata_pio_read(dev, buf, sizeof (buf))) {
		int sense = xenon_atapi_request_sense(dev);
		printf("DVD drive key read failed with sense: %06x\n", sense);
		return -1;
	};

	memcpy(dvdkey, buf, 0x10);
	return 0;
}

int
xenon_atapi_set_dvd_key(unsigned char *dvdkey) {
	struct xenon_ata_device *dev = &atapi;

	// create mode page buffer
	char buf[0x3A];
	memset(buf, 0, 0x3A);

	// Mode parameter header(mode select 10) start
	buf[0] = 0x00; // MODE DATA LENGTH MSB
	buf[1] = 0x38; // MODE DATA LENGTH LSB
	// Mode parameter header(mode select 10) end
	buf[8] = 0xBB; // mode page code (vendor specific, page format required)
	buf[9] = 0x30; // page length (16 bytes drive key + 32 bytes 0x00 following after mode page code)

	// set drive key in mode page to 0xFF
	memset(buf + 0x0A, 0xFF, 0x10);

	char cdb[12] = {0x55, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x3A, 0x00, 0x00, 0x00};


	xenon_atapi_packet(dev, cdb, 0);
	xenon_ata_wait_ready(dev);
	if (xenon_ata_pio_write(dev, buf, sizeof (buf))) {
		int sense = xenon_atapi_request_sense(dev);
		printf(" ! DVD drive key erase failed with sense: %06x\n", sense);
		return -1;
	};

	//set the key into the buffer
	memcpy(buf + 0x0A, dvdkey, 0x10);

	xenon_atapi_packet(dev, cdb, 0);
	xenon_ata_wait_ready(dev);
	if (xenon_ata_pio_write(dev, buf, sizeof (buf))) {
		int sense = xenon_atapi_request_sense(dev);
		printf("DVD drive key set failed with sense: %06x\n", sense);
		return -1;
	};

	return 0;
}

static int
xenon_atapi_read_sectors(sec_t start_sector, sec_t sector_size, void *buf) {
	int maxretries = 20;
	//struct xenon_ata_device *dev = bdev->ctx;
	struct xenon_ata_device *dev = &atapi;
	struct xenon_atapi_read readcmd;

	readcmd.code = 0x28;
	readcmd.lba = start_sector;
	readcmd.length = sector_size;
	
	// Wait for the drive to become ready before we attempt to read sectors
	// Hitachi 3120L and DL10N drives are picky and hang if we don't wait
	xenon_ata_wait_ready(dev);

#ifndef USE_DMA
	unsigned int sect = 0;
	void * bufpos = buf;
retry:
	xenon_atapi_packet(dev, (char *) &readcmd, 0);
	xenon_ata_wait_ready(dev);
	for (sect = 0; sect < sector_size; sect++) {
		if (xenon_ata_pio_read(dev, bufpos, XENON_CDROM_SECTOR_SIZE)) {
			int sense = xenon_atapi_request_sense(dev);

			// no media
			if (SK(sense) == 0x02 && ASC(sense) == 0x3a) {
				return DISKIO_ERROR_NO_MEDIA;
			}

			// becoming ready
			if ((SK(sense) == 0x02 && ASC(sense) == 0x4) || SK(sense) == 0x6) {
				if (!maxretries) return -1;
				mdelay(500);
				maxretries--;
				goto retry;
			}

			printf("ATAPI read error\n");
			return -1;
		}
		bufpos += XENON_CDROM_SECTOR_SIZE;
	}
#else
retry:
	xenon_atapi_packet(dev, (char *) &readcmd, 1);
	xenon_ata_wait_ready(dev);

	if (xenon_ata_dma_read(dev, buf, sector_size * XENON_CDROM_SECTOR_SIZE)) {
		int sense = xenon_atapi_request_sense(dev);

		// no media
		if (SK(sense) == 0x02 && ASC(sense) == 0x3a) {
			return DISKIO_ERROR_NO_MEDIA;
		}

		// becoming ready
		if ((SK(sense) == 0x02 && ASC(sense) == 0x4) || SK(sense) == 0x6) {
			if (!maxretries) return -1;
			mdelay(500);
			maxretries--;
			goto retry;
		}

		printf("ATAPI DMA read error\n");
		return -1;
	}
#endif

	return sector_size;
}

static int
xenon_ata_identify(struct xenon_ata_device *dev) {
	char info[XENON_DISK_SECTOR_SIZE];
	uint16_t *info16 = (uint16_t *) info;
	xenon_ata_wait_busy(dev);

	xenon_ata_regset(dev, XENON_ATA_REG_DISK, 0xE0);
	xenon_ata_regset(dev, XENON_ATA_REG_CMD, XENON_ATA_CMD_IDENTIFY_DEVICE);
	xenon_ata_wait_ready(dev);

	int val = xenon_ata_pio_read(dev, info, XENON_DISK_SECTOR_SIZE);
	if (val & 4)
		return xenon_atapi_inquiry_model(dev);
	else if (val)
		return -1;

	/* Now it is certain that this is not an ATAPI device.  */
	dev->atapi = 0;

	/* CHS is always supported.  */
	dev->addressing_mode = XENON_ATA_CHS;
	/* Check if LBA is supported.  */
	if (bswap_16(info16[49]) & (1 << 9)) {
		/* Check if LBA48 is supported.  */
		if (bswap_16(info16[83]) & (1 << 10))
			dev->addressing_mode = XENON_ATA_LBA48;
		else
			dev->addressing_mode = XENON_ATA_LBA;
	}

	/* Determine the amount of sectors.  */
	if (dev->addressing_mode != XENON_ATA_LBA48)
		dev->size = __builtin_bswap32(*((uint32_t *) & info16[60]));
	else
		dev->size = __builtin_bswap32(*((uint32_t *) & info16[100]));

	/* Read CHS information.  */
	dev->cylinders = bswap_16(info16[1]);
	dev->heads = bswap_16(info16[3]);
	dev->sectors_per_track = bswap_16(info16[6]);

	xenon_ata_dumpinfo(dev, info);

	return 0;
}

int
xenon_ata_init1(struct xenon_ata_device *dev, uint32_t ioaddress, uint32_t ioaddress2) {

	memset(dev, 0, sizeof (struct xenon_ata_device));
	dev->ioaddress = ioaddress;
	dev->ioaddress2 = ioaddress2;

	dev->prds = memalign(0x10000, MAX_PRDS * sizeof (struct xenon_ata_dma_prd));
	
	xenon_ata_identify(dev);
	
	/* Try to detect if the port is in use by writing to it,
	   waiting for a while and reading it again.  If the value
	   was preserved, there is a device connected.  */
	xenon_ata_regset(dev, XENON_ATA_REG_DISK, 0);
	xenon_ata_wait();
	xenon_ata_regset(dev, XENON_ATA_REG_LBALOW, 0x5A);
	xenon_ata_wait();
	if (xenon_ata_regget(dev, XENON_ATA_REG_LBALOW) != 0x5A) {
		printf("no ata device connected.\n");
		return -1;
	}

	/* Issue a reset.  */
	xenon_ata_regset2(dev, XENON_ATA_REG2_CONTROL, 6);
	xenon_ata_wait();
	xenon_ata_regset2(dev, XENON_ATA_REG2_CONTROL, 2);
	xenon_ata_regget2(dev, XENON_ATA_REG2_CONTROL);
	xenon_ata_regget2(dev, XENON_ATA_REG2_CONTROL);
	xenon_ata_regget2(dev, XENON_ATA_REG2_CONTROL);
	xenon_ata_regget2(dev, XENON_ATA_REG2_CONTROL);

	xenon_ata_regset(dev, XENON_ATA_REG_DISK, 0);
	xenon_ata_regget2(dev, XENON_ATA_REG2_CONTROL);
	xenon_ata_regget2(dev, XENON_ATA_REG2_CONTROL);
	xenon_ata_regget2(dev, XENON_ATA_REG2_CONTROL);
	xenon_ata_regget2(dev, XENON_ATA_REG2_CONTROL);

	printf("SATA device at %08lx\n", dev->ioaddress);

	return 0;
}

static int ata_ready = 0;
static int atapi_ready = 0;

static bool atapi_inserted() {
	return atapi_ready;
}

static bool ata_startup(void){
	return true;
}
static bool ata_inserted(void) {
	return ata_ready;
}
static bool ata_readsectors(sec_t sector, sec_t numSectors, void* buffer) {
	if(xenon_ata_read_sectors(sector,numSectors,buffer))
		return true;
	return false;
}
static bool atapi_readsectors(sec_t sector, sec_t numSectors, void* buffer) {
	if(xenon_atapi_read_sectors(sector,numSectors,buffer))
		return true;
	return false;
}
static bool ata_writesectors(sec_t sector, sec_t numSectors, const void* buffer){
	if(xenon_ata_write_sectors(sector,numSectors,buffer))
		return true;
	else
		return false;
}
static bool ata_clearstatus(void){
	/**todo*/
	return true;
}
static bool ata_shutdown(void){
	/**todo*/
	return true;
}

static s32 ata_sectors(void)
{
	struct xenon_ata_device *dev = &ata;
	return dev->size;
}

static s32 atapi_sectors(void)
{
	struct xenon_ata_device *dev = &atapi;
	return dev->size;
}

DISC_INTERFACE xenon_ata_ops = {
	.sectors = (FN_MEDIUM_DEVSECTORS) & ata_sectors,
	.readSectors = (FN_MEDIUM_READSECTORS) & ata_readsectors,
	.writeSectors = (FN_MEDIUM_WRITESECTORS) & ata_writesectors,
	.clearStatus = (FN_MEDIUM_CLEARSTATUS) & ata_clearstatus,
	.shutdown = (FN_MEDIUM_SHUTDOWN) & ata_shutdown,
	.isInserted = (FN_MEDIUM_ISINSERTED) & ata_inserted,
	.startup = (FN_MEDIUM_STARTUP) & ata_startup,
	.ioType = FEATURE_XENON_ATA,
	.features = FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_XENON_ATA,
};

DISC_INTERFACE xenon_atapi_ops = {
	.sectors = (FN_MEDIUM_DEVSECTORS) & atapi_sectors,
	.readSectors = (FN_MEDIUM_READSECTORS) & atapi_readsectors,
	.clearStatus = (FN_MEDIUM_CLEARSTATUS) & ata_clearstatus,
	.shutdown = (FN_MEDIUM_SHUTDOWN) & ata_shutdown,
	.isInserted = (FN_MEDIUM_ISINSERTED) & atapi_inserted,
	.startup = (FN_MEDIUM_STARTUP) & ata_startup,
	.ioType = FEATURE_XENON_ATAPI,
	.features = FEATURE_MEDIUM_CANREAD | FEATURE_XENON_ATAPI,
};

int
xenon_ata_init() {
	//preinit (start from scratch)
	*(volatile uint32_t*)0xd0110060 = __builtin_bswap32(0x112400);
	*(volatile uint32_t*)0xd0110080 = __builtin_bswap32(0x407231BE);
	*(volatile uint32_t*)0xea001318 &= __builtin_bswap32(0xFFFFFFF0);
	mdelay(1000);

	struct xenon_ata_device *dev = &ata;
	memset(dev, 0, sizeof (struct xenon_ata_device));
	int err = xenon_ata_init1(dev, 0xea001300, 0xea001320);
	if (!err)
		ata_ready = 1;
	return err;
}

int
xenon_atapi_init() {
	//preinit (start from scratch)
	*(volatile uint32_t*)0xd0108060 = __builtin_bswap32(0x112400);
	*(volatile uint32_t*)0xd0108080 = __builtin_bswap32(0x407231BE);
	*(volatile uint32_t*)0xea001218 &= __builtin_bswap32(0xFFFFFFF0);
	mdelay(200);

	struct xenon_ata_device *dev = &atapi;
	memset(dev, 0, sizeof (struct xenon_ata_device));
	int err = xenon_ata_init1(dev, 0xea001200, 0xea001220);
	if (!err)
		atapi_ready = 1;
	return err;
}
