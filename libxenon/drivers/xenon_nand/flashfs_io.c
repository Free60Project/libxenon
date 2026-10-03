/* flashfs_io.c - block/subpage I/O over raw NAND and flat eMMC */
#include "flashfs_priv.h"


/* Spare of a physical block's first page. */
int block_spare(const flashfs_t *fs, uint32_t b, uint8_t *sp)
{
    return subpage_read(fs, b * block_subpages(fs), NULL, sp);
}

/* Copy `len` data bytes of little block `blk` (holes are hard errors). */
int block_read(const flashfs_t *fs, uint32_t blk, uint8_t *out,
                      uint32_t len)
{
    uint32_t lba, phys, s0, k;
    static uint8_t pg[512];
    if (fs->type == FLASHFS_EMMC) {
        size_t off;
        if (blk >= fs->nblocks || len > FLASHFS_BLOCK_LEN)
            return -1;
        if (fs->img) {
            off = (size_t)blk * FLASHFS_BLOCK_LEN;
            if (off + len > fs->len)
                return -1;
            memcpy(out, fs->img + off, len);
            return 0;
        }
        /* live eMMC (img == NULL): fan out via backend subpages */
        for (k = 0; k < (len + 511u) / 512u; k++) {
            uint32_t cp = 512;
            if (k * 512u + cp > len)
                cp = len - k * 512u;
            if (subpage_read(fs, blk * 32u + k, pg, NULL) != 0)
                return -1;
            memcpy(out + k * 512u, pg, cp);
        }
        return 0;
    }
    if (blk >= fs->nblocks || len > FLASHFS_BLOCK_LEN ||
        fs->lils_per_block == 0)
        return -1;
    lba = blk / fs->lils_per_block;
    phys = (lba < fs->phys_blocks) ? fs->lba2phys[lba] : NO_PHYS;
    if (phys == NO_PHYS)
        return -1; /* hole: LBA unmapped */
    s0 = (blk % fs->lils_per_block) * 32u;
    for (k = 0; k < (len + 511u) / 512u; k++) {
        uint32_t g = phys * block_subpages(fs) + s0 + k;
        uint32_t cp = 512;
        if (k * 512u + cp > len)
            cp = len - k * 512u;
        if (subpage_read(fs, g, pg, NULL) != 0)
            return -1;
        memcpy(out + k * 512u, pg, cp);
    }
    return 0;
}
