/*
 * flashfs.h - NAND / eMMC flash filesystem library (Xbox 360 flashfs)
 *
 * Read-only parser for the Xbox 360 flash filesystem, based on the
 * RGBuildPP CXeFlashFileSystemRoot / CXeFlashBlockDriver structures.
 *
 * Works on raw NAND images in memory (spare/ECD included, as libxenon's
 * sfcx raw reads return them) or flat eMMC images; on hardware it reads
 * live flash via sfcx. In tests it is fed emulated dumps from _raw_dumps/.
 */
#ifndef _FLASHFS_H
#define _FLASHFS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Image layout: NAND is RAW data with spare/ECD (exactly what libxenon's
 * sfcx raw reads return); eMMC is flat logical data. FS discovery: NAND
 * scans spare sequence numbers (highest wins), eMMC reads Corona slots. */
typedef enum {
    FLASHFS_NAND = 0,
    FLASHFS_EMMC = 1
} flashfs_type_t;

/* Geometry (little blocks are always 0x4000 data bytes) */
#define FLASHFS_BLOCK_LEN   0x4000u
#define FLASHFS_PAGE_LEN    0x200u
#define FLASHFS_NAND_PAGEREAL 0x210u  /* 0x200 data + 0x10 spare */
#define FLASHFS_ENTRY_LEN   0x20u     /* sizeof on-disk entry */
#define FLASHFS_NAME_LEN    0x16u     /* 22 chars */

/* Blockmap markers (masked with 0x7fff before compare) */
#define FLASHFS_BLK_FREE    0x1ffeu   /* unallocated / end of entries */
#define FLASHFS_BLK_END     0x1fffu   /* end of chain */
#define FLASHFS_BLK_RSRVD   0x1ffbu   /* reserved (payloads, config, ...) */

typedef struct {
    char     name[FLASHFS_NAME_LEN + 1]; /* NUL-terminated */
    uint16_t block;   /* start block index */
    uint32_t size;    /* file size in bytes */
    uint32_t timestamp;
} flashfs_entry_t;

typedef struct {
    const uint8_t *img;   /* NULL on the live-flash backend */
    const void    *backend; /* set once at mount: emu or sfcx ops */
    size_t         len;
    flashfs_type_t type;
    uint16_t       root;       /* FS root (little-block index) */
    uint16_t       fs_offset;  /* data-block offset (bigblock NANDs) */
    int            version;    /* FS spare sequence version */
    uint16_t      *blockmap;   /* chain map, nblocks entries */
    uint32_t       nblocks;    /* little blocks */
    flashfs_entry_t *entries;
    uint32_t       nentries;
    /* NAND geometry + LBA map (unused for eMMC) */
    uint32_t       geom_page_data, geom_page_spare, geom_pages_block;
    int            geom_spare; /* spare layout */
    uint32_t       lils_per_block, phys_blocks;
    uint16_t      *lba2phys;   /* LBA -> physical block, NO_PHYS = hole */
} flashfs_t;

/*
 * Mount the filesystem whose root lives at block `root`.
 * Returns 0 on success, -1 on bad image/root. Parses filetable eagerly.
 * `fs` must be zero-initialized (`flashfs_t fs = {0}`) or unmounted;
 * mount/scan free any previous tables, so remounting is safe.
 */
int flashfs_mount(flashfs_t *fs, const uint8_t *img, size_t len,
                  flashfs_type_t type, uint16_t root);

/*
 * Auto-locate the FS root: NAND scans spare sequence numbers
 * (highest wins); eMMC reads Corona FS slots (if in range,
 * highest version wins).
 * Returns 0 on success, -1 if no filesystem found.
 */
int flashfs_scan(flashfs_t *fs, const uint8_t *img, size_t len,
                 flashfs_type_t type);

void flashfs_unmount(flashfs_t *fs);

uint32_t flashfs_count(const flashfs_t *fs);
const flashfs_entry_t *flashfs_get(const flashfs_t *fs, uint32_t index);
const flashfs_entry_t *flashfs_find(const flashfs_t *fs, const char *name);

/*
 * Read up to `len` bytes of a file at file-offset `off` into `out`.
 * Returns bytes read, 0 at EOF, -1 on error.
 */
#if defined(LIBXENON) || defined(XENON)
/*
 * Live-flash backend for libxenon (read-only, NAND + eMMC). NAND reads go
 * through sfcx_read_page(raw=1) (geometry/spare from struct sfc, sfcx_init
 * runs lazily); eMMC reads go through xenon_get_logical_nand_data.
 */
int flashfs_mount_hw(flashfs_t *fs, uint16_t root);
int flashfs_scan_hw(flashfs_t *fs);
#endif
int flashfs_read(const flashfs_t *fs, const flashfs_entry_t *e,
                 uint8_t *out, uint32_t off, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* _FLASHFS_H */
