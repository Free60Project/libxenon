#include <xenon_nand/xenon_emmc.h>
#include <xenon_nand/xenon_sfcx.h>
#include <ppc/timebase.h>
#include <time/time.h>
#include <stdio.h>
#include <string.h>

uint32_t emmc_istatus;
char* emmc_pio_write_buf;
int emmc_init_status = 0;

#define EMMC_DMA_TRANSFER_BUF_SZ 0x1000
#define EMMC_DMA_DESCRIPTOR_BUF_SZ 0x100
#define EMMC_TIMEOUT_MS 100

/*
    DMA Transfer buffer
*/
__attribute__ ((aligned (EMMC_DMA_TRANSFER_BUF_SZ)))
volatile char emmc_dma_buffer[EMMC_DMA_TRANSFER_BUF_SZ];

volatile void* get_emmc_dma_buffer(){
    return &emmc_dma_buffer;
}

/*
    DMA Descriptors might not need this much memory or alignment, at least how they are currently used
    DMA Descriptors consist of 2 uint32_t
        0x0 attributes (0x10000023 is used). A length can likely be encoded here if multiple descriptors are used.
        0x4 physical memory address
*/
__attribute__ ((aligned (EMMC_DMA_DESCRIPTOR_BUF_SZ)))
volatile char emmc_dma_descriptors[EMMC_DMA_DESCRIPTOR_BUF_SZ];

// Note: Access to PIO FIFO is in Native Endianess
void emmc_peek_pio_buffer(char * output){
    for(int i = 0; i < 0x200; i+=4){
        *(uint32_t*)(&output[i]) = *(volatile unsigned int*)(0xea00c000UL | EMMC_PIO_BUFFER_REG);
    }
}

// Note: Access to PIO FIFO is in Native Endianess
void emmc_poke_pio_buffer(char * output){
    for(int i = 0; i < 0x200; i+=4){
        *(volatile unsigned int*)(0xea00c000UL | EMMC_PIO_BUFFER_REG) = *(uint32_t*)(&output[i]);
    }
}

int emmc_cmd(uint8_t cmd, uint32_t argument, int flags){

    uint32_t cmd_reg = (cmd<<24) | 0x1A0000;
    if((flags&EMMC_FLAG_DMA_READ) || (flags&EMMC_FLAG_DMA_WRITE)){
        uint32_t dma_addr = ((uint32_t)&emmc_dma_descriptors) & 0x1FFFFFFFUL;
        sfcx_writereg(EMMC_DMA_DESCRIPTOR_REG, dma_addr);
        sfcx_writereg(EMMC_DMA_UNK_REG, 0);
        if(flags&EMMC_FLAG_DMA_READ){
            cmd_reg |= 0x200033; // read
        } else {
            cmd_reg |= 0x200023; // write
        }
    }
    if((flags&EMMC_FLAG_PIO_READ) || (flags&EMMC_FLAG_PIO_WRITE)){
        cmd_reg |= 0x200000;
    }
    if(flags&EMMC_FLAG_PIO_READ){
        cmd_reg |= 0x10;
    }
    if(flags&EMMC_FLAG_PIO_WRITE){
        cmd_reg |= 0x80;
    }
    //printf("emmc_cmd: CMD->%08X\n", cmd_reg);

    sfcx_writereg(EMMC_TIMEOUT_REG, 0x80200);
    sfcx_writereg(EMMC_ARGUMENT_REG, argument);
    sfcx_writereg(EMMC_COMMAND_REG, cmd_reg);

    //int old_emmc_istatus = 0;
    uint64_t start_time = mftb();
    emmc_istatus = 0;
    while(1){
        emmc_istatus = sfcx_readreg(EMMC_INTERRUPT_STATUS);
        //if(old_emmc_istatus != emmc_istatus){
        //    printf("emmc_istatus -> %08X\n", emmc_istatus);
        //    old_emmc_istatus = emmc_istatus;
        //}
        if((cmd == EMMC_CMD_READ_MULTIPLE_BLOCK) && (emmc_istatus == 3)){
            break;
        }
        if((cmd == EMMC_CMD_WRITE_MULIPLE_BLOCK) && (emmc_istatus == 3)){
            break;
        }
        if((cmd != EMMC_CMD_WRITE_MULIPLE_BLOCK) && (emmc_istatus != 0)){
            break;
        }

        if(tb_diff_msec(mftb(), start_time) > EMMC_TIMEOUT_MS){
            if(emmc_istatus != 0){
                sfcx_writereg(EMMC_INTERRUPT_STATUS, emmc_istatus);
            }
            printf("emmc cmd%d arg %08X timeout istat=%08X\n", cmd, argument, emmc_istatus);
            return -1;
        }
    }
    sfcx_writereg(EMMC_INTERRUPT_STATUS, emmc_istatus);
    if(emmc_istatus >= 0x8000){
        printf("emmc error, CMD%d ARG %08X emmc_istatus=%08X\n", cmd, argument, emmc_istatus);
        return -2;
    }

    //printf("emmc_cmd: CMD%d istatus %08X\n", cmd, emmc_istatus);
    if(flags&EMMC_FLAG_PIO_WRITE){
        emmc_poke_pio_buffer(emmc_pio_write_buf);
    }

    start_time = mftb();
    if((flags&EMMC_FLAG_PIO_READ) || (flags&EMMC_FLAG_PIO_WRITE)){
        //printf("emmc_transfer_wait\n");
        uint32_t status = sfcx_readreg(EMMC_STATUS_REG);
        while((status & 3) != 0){
            status = sfcx_readreg(EMMC_STATUS_REG);
            if(tb_diff_msec(mftb(), start_time) > EMMC_TIMEOUT_MS){
                printf("emmc cmd%d arg %08X pio transfer timeout: stat=%08X\n", cmd, argument, status);
                return -3;
            }
        }
        //printf("emmc_stat: %08X\n", status);
    }

    return 0;
}

void emmc_set_pio_write_buffer(char* buffer){
    emmc_pio_write_buf = buffer;
}

void emmc_setup_dma(){
    *((uint32_t*)(&emmc_dma_descriptors[0])) = __builtin_bswap32(0x10000023L);
    uint32_t dma_addr = ((uint32_t)&emmc_dma_buffer) & 0x1FFFFFFFUL;
    *((uint32_t*)(&emmc_dma_descriptors[4])) = __builtin_bswap32(dma_addr);
    asm volatile("sync");
}

int emmc_init(){
    char buffer[0x200];

    // Fumble our way through controller reset, and try not to drop the ball
    unsigned int control_reg = sfcx_readreg(EMMC_CONTROL_REG);
    control_reg &= 0xFFFFFFFAUL;
    sfcx_writereg(EMMC_CONTROL_REG, control_reg);
    mdelay(10);
    control_reg = sfcx_readreg(EMMC_CONTROL_REG);
    control_reg &= 0xFFFF00FFUL;
    sfcx_writereg(EMMC_CONTROL_REG, control_reg);
    mdelay(10);
    control_reg = sfcx_readreg(EMMC_CONTROL_REG);
    control_reg |= 5;
    sfcx_writereg(EMMC_CONTROL_REG, control_reg);
    mdelay(10);

    control_reg = sfcx_readreg(EMMC_UNK_F0_REG);
    control_reg&=0xFFFFFFEF;
    sfcx_writereg(EMMC_UNK_F0_REG, control_reg);

    mdelay(50);

    //Clear Pending Interrupt Status
    sfcx_writereg(EMMC_INTERRUPT_STATUS, sfcx_readreg(EMMC_INTERRUPT_STATUS));

    // Select Card. Apparently RCA can be set to FFFFFFFF
    emmc_cmd(EMMC_CMD_SEL_CARD, 0xFFFFFFFFUL, EMMC_FLAG_NONE);

    // Get Extended CSD
    emmc_cmd(EMMC_CMD_SEND_EXT_CSD, 0, EMMC_FLAG_PIO_READ);
    emmc_peek_pio_buffer(buffer);

    // Sanity Check: Do we have a sector count?
    uint32_t num_sectors = __builtin_bswap32(*(uint32_t*)(&buffer[212]));
    printf("emmc_num_sectors: %08X\n", num_sectors);
    if(num_sectors == 0){
        printf("emmc_num_sectors=0: failed to initialize emmc controller\n");
        emmc_init_status = -1;
        return -1;
    }

    // Initialize DMA descriptors and Buffer
    memset((void*)emmc_dma_descriptors, 0, EMMC_DMA_DESCRIPTOR_BUF_SZ);
    memset((void*)emmc_dma_buffer, 0, EMMC_DMA_TRANSFER_BUF_SZ);

    emmc_init_status = 1;

    return 0;
}

int emmc_rawflash_writeImage(int len, int f){

    if(emmc_init_status <= 0){
        emmc_init();
    }

    char* dmabuf = (char*)get_emmc_dma_buffer();
    for(int offset = 0; offset < EMMC_NAND_48; offset+=EMMC_DMA_TRANSFER_BUF_SZ){
        if((offset & 0x3FFF) == 0){
            printf("... write block 0x%x\n", offset>>14);
        }
        if(read(f, dmabuf, EMMC_DMA_TRANSFER_BUF_SZ) < 0){
            printf("failed to read source file @ 0x%08X\n", offset);
            return -1;
        }
        asm volatile("sync");
        
        // Note: Error handling logic is not well tested, as errors currently aren't happening during testing
        int retry_count = 0;
        retry:
        if(retry_count >= 5){
            printf("... eMMC: too many retries, aborting operation\n");
            return -1;
        }
        if(emmc_cmd(EMMC_CMD_SET_BLOCK_COUNT, 8, EMMC_FLAG_NONE) < 0){
            printf("... eMMC failed to SET_BLOCK_COUNT for write, retry\n");
            mdelay(500);
            retry_count += 1;
            goto retry;
        }
        emmc_setup_dma();
        if(emmc_cmd(EMMC_CMD_WRITE_MULIPLE_BLOCK, offset>>9, EMMC_FLAG_DMA_WRITE) < 0){
            printf("... eMMC failed to WRITE_MULIPLE_BLOCK, retry\n");
            mdelay(500);
            retry_count += 1;
            goto retry;
        }
    }
    return 1;
}
