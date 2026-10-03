/* flashfs_io.c - flash access: NAND geometry, block reads, file reads */
#include "flashfs_priv.h"
#include "xenon_nand/xenon_sfcx.h"
#include "xb360/xb360.h"

/* Take geometry from the sfc driver (initialising it if needed). Only 512B
 * pages are supported: the raw page reader returns one subpage at a time. */
int _flashfs_load_geom(flashfs_t *fs)
{
    if (sfc.initialized != SFCX_INITIALIZED) {
        sfcx_init();
        if (sfc.initialized != SFCX_INITIALIZED)
            return -1;
    }
    if (sfc.page_sz != 512 || sfc.block_sz % FLASHFS_BLOCK_LEN != 0)
        return -1;
    fs->page_data = (uint32_t)sfc.page_sz;
    fs->page_spare = (uint32_t)sfc.meta_sz;
    fs->pages_block = (uint32_t)sfc.pages_in_block;
    if (sfc.meta_type == 0)
        fs->spare = SPARE_SB;
    else if (sfc.meta_type == 1 && sfc.pages_in_block == 32)
        fs->spare = SPARE_JASPER;
    else
        fs->spare = SPARE_BB;
    fs->lils_per_block = (uint32_t)sfc.block_sz / FLASHFS_BLOCK_LEN;
    fs->phys_blocks = (uint32_t)sfc.size_blocks;
    fs->nblocks = fs->phys_blocks * fs->lils_per_block;
    return 0;
}

/* Raw page read of NAND page `page` into data (512B) and/or spare. */
static int nand_page(const flashfs_t *fs, uint32_t page, uint8_t *data,
                     uint8_t *spare)
{
    static uint8_t raw[0x880];
    if (sfcx_read_page(raw, (int)(page * fs->page_data), 1) < 0)
        return -1;
    if (data)
        memcpy(data, raw, fs->page_data);
    if (spare)
        memcpy(spare, raw + fs->page_data, fs->page_spare);
    return 0;
}

/* Spare area of the first page of physical block `phys`; this is where the
 * LBA and sequence number are stored. */
int _flashfs_block_spare(const flashfs_t *fs, uint32_t phys, uint8_t *sp)
{
    return nand_page(fs, phys * fs->pages_block, NULL, sp);
}

/* Read `len` bytes of little block `blk`. An unmapped LBA is an error rather
 * than 0xFF data, so a damaged NAND can't be mistaken for an empty file. */
int _flashfs_block_read(const flashfs_t *fs, uint32_t blk, uint8_t *out,
                        uint32_t len)
{
    uint32_t lba, phys, page, k;
    static uint8_t pg[512];

    if (blk >= fs->nblocks || len > FLASHFS_BLOCK_LEN)
        return -1;
    if (fs->emmc)
        return xenon_get_logical_nand_data(out, blk * FLASHFS_BLOCK_LEN, len);

    lba = blk / fs->lils_per_block;
    phys = (lba < fs->phys_blocks) ? fs->lba2phys[lba] : NO_PHYS;
    if (phys == NO_PHYS)
        return -1;
    page = phys * fs->pages_block +
           (blk % fs->lils_per_block) * (FLASHFS_BLOCK_LEN / fs->page_data);
    for (k = 0; k * 512u < len; k++) {
        uint32_t cp = len - k * 512u < 512 ? len - k * 512u : 512;
        if (nand_page(fs, page + k, pg, NULL) != 0)
            return -1;
        memcpy(out + k * 512u, pg, cp);
    }
    return 0;
}

/* Copy up to `len` bytes of `e` starting at file offset `off` by walking its
 * block chain. Returns bytes copied (0 at EOF) or -1. */
int _flashfs_read(const flashfs_t *fs, const flashfs_entry_t *e,
                  uint8_t *out, uint32_t off, uint32_t len)
{
    static uint8_t blk[FLASHFS_BLOCK_LEN];
    uint32_t cur = e->block, skip = off, total = 0, guard = 0;

    if (off >= e->size)
        return 0;
    if (len > e->size - off)
        len = e->size - off;
    while (len > 0 && guard++ < fs->nblocks) {
        if (cur >= fs->nblocks)
            return -1;
        if (skip >= FLASHFS_BLOCK_LEN) {
            skip -= FLASHFS_BLOCK_LEN; /* whole block precedes the range */
        } else {
            uint32_t cp = FLASHFS_BLOCK_LEN - skip;
            if (cp > len)
                cp = len;
            if (_flashfs_block_read(fs, cur + fs->fs_offset, blk,
                                    sizeof(blk)) != 0)
                return -1;
            memcpy(out + total, blk + skip, cp);
            skip = 0;
            total += cp;
            len -= cp;
        }
        cur = fs->blockmap[cur] & 0x7fffu;
        if (cur == FLASHFS_BLK_FREE || cur == FLASHFS_BLK_END)
            break;
    }
    return (int)total;
}
