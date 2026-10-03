/* flashfs_spare.c - NAND geometry detect, LBA map, spare-ordered FS discovery */
#include "flashfs_priv.h"

/* Decode first-page spare of a physical block. */
static void spare_decode(const flashfs_t *fs, const uint8_t *sp, int *bad,
                         uint32_t *lba, uint32_t *seq, int *type)
{
    switch (fs->geom_spare) {
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
/* ---- NAND: geometry, LBA map, spare-ordered discovery ---- */

static int spare_erased(const uint8_t *sp, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if (sp[i] != 0xFF)
            return 0;
    return 1;
}

/* Validate geometry: whole blocks; block 0/1 spares decode to LBA
 * 0/1, good, non-erased. */
int geom_valid(flashfs_t *fs, const nand_geom_t *g)
{
    uint8_t sp[64];
    uint32_t save[4];
    int bad, type, ok = 0;
    uint32_t lba, seq;
    uint32_t block_real =
        g->pages_per_block * (g->page_data + g->page_spare);
    if (fs->len % block_real != 0 || fs->len < block_real * 2)
        return 0;
    save[0] = fs->geom_page_data;
    save[1] = fs->geom_page_spare;
    save[2] = fs->geom_pages_block;
    save[3] = (uint32_t)fs->geom_spare;
    fs->geom_page_data = g->page_data;
    fs->geom_page_spare = g->page_spare;
    fs->geom_pages_block = g->pages_per_block;
    fs->geom_spare = g->spare;
    /* block 0 and 1 first-page spares must decode to LBA 0 and 1 */
    if (subpage_read(fs, 0, NULL, sp) == 0 &&
        !spare_erased(sp, g->page_spare)) {
        spare_decode(fs, sp, &bad, &lba, &seq, &type);
        if (!bad && lba == 0) {
            uint32_t g1 =
                g->pages_per_block * (g->page_data / 512u);
            if (subpage_read(fs, g1, NULL, sp) == 0 &&
                !spare_erased(sp, g->page_spare)) {
                spare_decode(fs, sp, &bad, &lba, &seq, &type);
                ok = !bad && lba == 1;
            }
        }
    }
    fs->geom_page_data = save[0];
    fs->geom_page_spare = save[1];
    fs->geom_pages_block = save[2];
    fs->geom_spare = (int)save[3];
    return ok;
}

int nand_alloc(flashfs_t *fs)
{
    uint32_t b;
    fs->lba2phys = malloc((size_t)fs->phys_blocks * sizeof(uint16_t));
    fs->blockmap = malloc((size_t)fs->nblocks * sizeof(uint16_t));
    fs->entries = malloc((size_t)fs->nblocks * sizeof(flashfs_entry_t));
    if (!fs->lba2phys || !fs->blockmap || !fs->entries) {
        flashfs_unmount(fs);
        return -1;
    }
    for (b = 0; b < fs->phys_blocks; b++)
        fs->lba2phys[b] = NO_PHYS;
    memset(fs->blockmap, 0xfe, (size_t)fs->nblocks * sizeof(uint16_t));
    return 0;
}

/* One spare pass: build LBA map, collect FS candidates. */
uint32_t nand_scan_map(flashfs_t *fs, fs_candidate_t *cands,
                              uint32_t maxcands)
{
    uint32_t b, ncands = 0;
    for (b = 0; b < fs->phys_blocks; b++) {
        uint8_t sp[64];
        int bad, type;
        uint32_t lba, seq, k;
        if (block_spare(fs, b, sp) != 0)
            continue;
        if (spare_erased(sp, fs->geom_page_spare))
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

/* Highest seq wins (ties: lowest lil); candidates must parse sanely. */
uint32_t nand_pick(flashfs_t *fs, fs_candidate_t *cands,
                          uint32_t ncands, uint32_t *version)
{
    uint32_t k, best = 0xFFFFu, best_seq = 0;
    for (k = 0; k < ncands; k++) {
        if (cands[k].seq < best_seq)
            continue;
        if (cands[k].seq == best_seq && cands[k].lil >= best)
            continue;
        if (!table_sane(fs, cands[k].lil))
            continue;
        best_seq = cands[k].seq;
        best = cands[k].lil;
    }
    *version = best_seq;
    return best;
}

/* Spare seq of the physical block holding root lil (0 if unreadable). */
uint32_t root_version(flashfs_t *fs, uint16_t root)
{
    uint8_t sp[64];
    uint32_t lba = root / (fs->lils_per_block ? fs->lils_per_block : 1);
    int bad, type;
    uint32_t dlba, seq = 0;
    if (lba >= fs->phys_blocks || fs->lba2phys[lba] == NO_PHYS)
        return 0;
    if (block_spare(fs, fs->lba2phys[lba], sp) != 0)
        return 0;
    if (spare_erased(sp, fs->geom_page_spare))
        return 0;
    spare_decode(fs, sp, &bad, &dlba, &seq, &type);
    return bad ? 0 : seq;
}
