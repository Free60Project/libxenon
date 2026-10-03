/* flashfs_priv.h - shared internals (not public API)
 *
 * Backend selection lives here: mount sets fs->backend once to either
 * the emulated-image ops or the libxenon sfcx ops, and all I/O fans out
 * through it. No per-callsite #ifdefs.
 */
#ifndef _FLASHFS_PRIV_H
#define _FLASHFS_PRIV_H

#include <stdlib.h>
#include <string.h>
#include "flashfs.h"

#define CORONA_FS_ADDR  0x2FE8000u
#define HW_WINDOW       0x4000000u
#define MAX_CANDIDATES  64
#define NO_PHYS         0xFFFFu

/* NAND spare layouts */
#define SPARE_SB      0 /* small block: bad sp[5], idx sp[0..1] */
#define SPARE_JASPER  1 /* Jasper SB: bad sp[5], idx sp[1..2] */
#define SPARE_BB      2 /* big block: bad sp[0], idx sp[1..2] */

typedef struct {
    uint32_t page_data, page_spare, pages_per_block;
    int spare;
} nand_geom_t;

typedef struct {
    uint16_t lil;
    uint32_t seq;
} fs_candidate_t;

typedef struct {
    /* Read 512B data + spare of global subpage g (either out may be
     * NULL; spare buffer must hold 64 bytes). */
    int (*subpage)(const flashfs_t *fs, uint32_t g, uint8_t *data,
                   uint8_t *spare);
} flashfs_backend_t;

extern const flashfs_backend_t flashfs_emu_backend;
extern const flashfs_backend_t flashfs_hw_backend;

static inline uint16_t rd16be(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint32_t page_real(const flashfs_t *fs)
{
    return fs->geom_page_data + fs->geom_page_spare;
}

static inline uint32_t block_real(const flashfs_t *fs)
{
    return fs->geom_pages_block * page_real(fs);
}

static inline uint32_t block_subpages(const flashfs_t *fs)
{
    return fs->geom_pages_block * (fs->geom_page_data / 512u);
}

static inline int subpage_read(const flashfs_t *fs, uint32_t g,
                               uint8_t *data, uint8_t *spare)
{
    const flashfs_backend_t *be =
        fs ? (const flashfs_backend_t *)fs->backend : NULL;
    if (!be || !be->subpage)
        return -1;
    return be->subpage(fs, g, data, spare);
}

/* emu.c */
int emu_load_geom(flashfs_t *fs);

/* io.c */
int block_spare(const flashfs_t *fs, uint32_t b, uint8_t *sp);
int block_read(const flashfs_t *fs, uint32_t blk, uint8_t *out,
               uint32_t len);

/* emu.c */
int emu_load_geom(flashfs_t *fs);

/* spare.c */
int geom_valid(flashfs_t *fs, const nand_geom_t *g);
int nand_alloc(flashfs_t *fs);
uint32_t nand_scan_map(flashfs_t *fs, fs_candidate_t *cands,
                       uint32_t maxcands);
uint32_t nand_pick(flashfs_t *fs, fs_candidate_t *cands, uint32_t ncands,
                   uint32_t *version);
uint32_t root_version(flashfs_t *fs, uint16_t root);

/* table.c */
int table_sane(const flashfs_t *fs, uint32_t blk);
int mount_root(flashfs_t *fs, uint16_t root, int version);

#endif /* _FLASHFS_PRIV_H */
