/* flashfs_spare.c - NAND LBA map and spare-sequence FS root discovery */
#include "flashfs_priv.h"

/* Decode the first-page spare of a physical block; layout depends on the
 * NAND generation. `type` is the block's page type (0x30/0x2C = filesystem). */
static void spare_decode(const flashfs_t *fs, const uint8_t *sp, int *bad,
                         uint32_t *lba, uint32_t *seq, int *type)
{
    switch (fs->spare) {
    case SPARE_JASPER:
        *bad = sp[5] != 0xFF;
        *lba = ((sp[2] & 0xF) << 8) | sp[1];
        *seq = sp[0] | ((uint32_t)sp[3] << 8) | ((uint32_t)sp[4] << 16) |
               ((uint32_t)sp[6] << 24);
        break;
    case SPARE_BB:
        *bad = sp[0] != 0xFF;
        *lba = ((sp[2] & 0xF) << 8) | sp[1];
        *seq = sp[5] | ((uint32_t)sp[4] << 8) | ((uint32_t)sp[3] << 16);
        break;
    default: /* SPARE_SB */
        *bad = sp[5] != 0xFF;
        *lba = ((sp[1] & 0xF) << 8) | sp[0];
        *seq = sp[2] | ((uint32_t)sp[3] << 8) | ((uint32_t)sp[4] << 16) |
               ((uint32_t)sp[6] << 24);
        break;
    }
    *type = sp[0x0C] & 0x3F;
}
static int spare_erased(const uint8_t *sp, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if (sp[i] != 0xFF)
            return 0;
    return 1;
}

/* One pass over every block's spare: fill lba2phys and collect filesystem
 * root candidates. Returns the candidate count. */
uint32_t _flashfs_nand_scan(flashfs_t *fs, fs_candidate_t *cands,
                            uint32_t maxcands)
{
    uint32_t b, ncands = 0;
    for (b = 0; b < fs->phys_blocks; b++) {
        uint8_t sp[64];
        int bad, type;
        uint32_t lba, seq, k;
        if (_flashfs_block_spare(fs, b, sp) != 0)
            continue;
        if (spare_erased(sp, fs->page_spare))
            continue;
        spare_decode(fs, sp, &bad, &lba, &seq, &type);
        if (bad || lba >= fs->phys_blocks)
            continue;
        if (fs->lba2phys[lba] == NO_PHYS)
            fs->lba2phys[lba] = (uint16_t)b; /* first claimant wins */
        if ((type == 0x30 || type == 0x2C) && seq != 0 &&
            ncands < maxcands) {
            /* table lives in the block's first lil; siblings are an
             * unproven fallback (harmless: only tried on failure) */
            for (k = 0; k < fs->lils_per_block; k++) {
                cands[ncands].lil =
                    (uint16_t)(lba * fs->lils_per_block + k);
                cands[ncands].seq = seq;
                ncands++;
                if (ncands >= maxcands)
                    break;
            }
        }
    }
    return ncands;
}

/* Pick the candidate with the highest sequence (ties: lowest lil) whose table
 * looks sane, so a half-written newer root doesn't win. Returns 0xFFFF if none. */
uint32_t _flashfs_nand_pick(flashfs_t *fs, const fs_candidate_t *cands,
                            uint32_t ncands)
{
    uint32_t k, best = 0xFFFFu, best_seq = 0;
    for (k = 0; k < ncands; k++) {
        if (cands[k].seq < best_seq)
            continue;
        if (cands[k].seq == best_seq && cands[k].lil >= best)
            continue;
        if (!_flashfs_table_sane(fs, cands[k].lil))
            continue;
        best_seq = cands[k].seq;
        best = cands[k].lil;
    }
    return best;
}
