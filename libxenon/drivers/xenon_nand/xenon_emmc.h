#ifndef __xenon_emmc_h
#define __xenon_emmc_h

#include <unistd.h>

/*

Notes - SK1080 2025

MMCX appears to have multiple modes
    - A PIO transfer mode provides a IO mapped FIFO (Reg 0x20)
    - DMA transfer mode works by filling out a descriptor and then writing the address of it to Reg 0x58
        This is much faster, so we will use this for read/write

This driver is pretty rough, as the interface to eMMC isn't fully known.
If lucky maybe this interface is shared with some other eMMC controller out in the wild and more information may be discovered

The KSB takes care of the early eMMC initialization. Once we get through the reset sequence the first command needed is CMD7.

SPI can access the first 0x40 of eMMC registers, which is enough for PIO operation.
Note that the reset sequence here won't work on SPI, and that a slower clock rate may be required compared to SFCX.
If you don't reset, note that the card/interface will be setup to use byte addressing rather than block(512b) addressing.
This can be used to make a scuff SPI flasher, if you prefer having a common interface between console types when externally flashing.

This driver could use some improvements such as:
    - Variable size DMA transfers
    - Direct DMA to user rather than using a static transfer buffer built into the driver
    - Better testing and implementation of error handling code
    - Larger PIO transfers (is this possible?)
*/

// MMCX Registers
#define EMMC_TIMEOUT_REG 0x4
#define EMMC_ARGUMENT_REG 0x8
#define EMMC_COMMAND_REG 0xC
#define EMMC_RESPONSE0 0x10
#define EMMC_PIO_BUFFER_REG 0x20
#define EMMC_STATUS_REG 0x24
#define EMMC_CONTROL_REG 0x2C
#define EMMC_INTERRUPT_STATUS 0x30
#define EMMC_DMA_DESCRIPTOR_REG 0x58
#define EMMC_DMA_UNK_REG 0x5C
#define EMMC_UNK_F0_REG 0xF0

// (Incomplete) definition of eMMC Commands
#define EMMC_CMD_SEL_CARD 7
#define EMMC_CMD_SEND_EXT_CSD 8
#define EMMC_CMD_SEND_STATUS 13
#define EMMC_CMD_SET_BLOCK_LEN 16
#define EMMC_CMD_READ_SINGLE_BLOCK 17
#define EMMC_CMD_READ_MULTIPLE_BLOCK 18
#define EMMC_CMD_SET_BLOCK_COUNT 23
#define EMMC_CMD_WRITE_BLOCK 24
#define EMMC_CMD_WRITE_MULIPLE_BLOCK 25

// eMMC Interface flags
#define EMMC_FLAG_NONE 0        // Just perform a basic command
#define EMMC_FLAG_PIO_READ 1    // Perform a transfer to PIO FIFO
#define EMMC_FLAG_PIO_WRITE 2   // Perform a transfer from PIO FIFO
#define EMMC_FLAG_DMA_READ 4         // Perform a DMA transfer from eMMC to system
#define EMMC_FLAG_DMA_WRITE 8         // Perform a DMA transfer from system to eMMC

// eMMC Interface Functions
int emmc_init(); // initialize eMMC controller. Returns 0 on success.
int emmc_cmd(uint8_t cmd, uint32_t argument, int flags); // issue a eMMC command. optional: PIO or DMA transfer using flags

void emmc_peek_pio_buffer(char * output); // copy PIO buffer to output (size is 0x200)
void emmc_set_pio_write_buffer(char* buffer);  // set buffer to be used during PIO write operation (size is 0x200)

volatile void* get_emmc_dma_buffer(); // get bidirectional DMA buffer. length is 0x1000
void emmc_setup_dma(); //prepare DMA transfer

// high level API
int emmc_rawflash_writeImage(int len, int f);

#endif