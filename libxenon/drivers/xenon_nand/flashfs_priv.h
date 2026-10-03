/* flashfs_priv.h - internals shared between the flashfs_*.c files */
#ifndef _FLASHFS_PRIV_H
#define _FLASHFS_PRIV_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Little blocks are always 0x4000 data bytes, regardless of NAND geometry. */
#define FLASHFS_BLOCK_LEN   0x4000u
#define FLASHFS_PAGE_LEN    0x200u
#define FLASHFS_ENTRY_LEN   0x20u
#define FLASHFS_NAME_LEN    0x16u

/* Blockmap values (masked with 0x7fff). */
#define FLASHFS_BLK_FREE    0x1ffeu
#define FLASHFS_BLK_END     0x1fffu
#define FLASHFS_BLK_RSRVD   0x1ffbu

/* eMMC consoles keep two copies of the root pointer here; newest wins. */
#define CORONA_FS_ADDR  0x2FE8000u
/* Upper bound for a plausible file size, used to reject corrupt tables. */
#define HW_WINDOW       0x4000000u
#define MAX_CANDIDATES  64
#define NO_PHYS         0xFFFFu

/* NAND spare layouts: where the bad-block marker, LBA and sequence live. */
#define SPARE_SB      0 /* small block: bad sp[5], lba sp[0..1] */
#define SPARE_JASPER  1 /* Jasper small block: bad sp[5], lba sp[1..2] */
#define SPARE_BB      2 /* big block: bad sp[0], lba sp[1..2] */

typedef struct {
    char     name[FLASHFS_NAME_LEN + 1];
    uint16_t block;     /* first little block of the file's chain */
    uint32_t size;
    uint32_t timestamp;
} flashfs_entry_t;

typedef struct {
    int      emmc;          /* flat logical eMMC vs raw NAND with spare */
    uint32_t nblocks;       /* little blocks */
    uint16_t fs_offset;     /* added to chain indices to reach file data (big-block NAND) */
    uint16_t *blockmap;     /* next-block chain, nblocks entries */
    flashfs_entry_t *entries;
    uint32_t nentries;
    /* NAND only */
    uint32_t page_data, page_spare, pages_block;
    int      spare;         /* SPARE_* */
    uint32_t lils_per_block, phys_blocks;
    uint16_t *lba2phys;     /* LBA -> physical block, NO_PHYS = unmapped */
} flashfs_t;

typedef struct {
    uint16_t lil;
    uint32_t seq;
} fs_candidate_t;

static inline uint16_t rd16be(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* flashfs_io.c */
int _flashfs_load_geom(flashfs_t *fs);
int _flashfs_block_spare(const flashfs_t *fs, uint32_t phys, uint8_t *sp);
int _flashfs_block_read(const flashfs_t *fs, uint32_t blk, uint8_t *out,
                        uint32_t len);
int _flashfs_read(const flashfs_t *fs, const flashfs_entry_t *e,
                  uint8_t *out, uint32_t off, uint32_t len);

/* flashfs_spare.c */
uint32_t _flashfs_nand_scan(flashfs_t *fs, fs_candidate_t *cands,
                            uint32_t maxcands);
uint32_t _flashfs_nand_pick(flashfs_t *fs, const fs_candidate_t *cands,
                            uint32_t ncands);

/* flashfs_table.c */
int _flashfs_table_sane(const flashfs_t *fs, uint32_t blk);
int _flashfs_mount_root(flashfs_t *fs, uint16_t root);

/* flashfs.c */
void _flashfs_free(flashfs_t *fs);

#endif /* _FLASHFS_PRIV_H */
