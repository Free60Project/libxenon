/*
 * flashfs.c - NAND / eMMC flash filesystem library (Xbox 360 flashfs)
 *
 * Layout mirrors RGBuildPP CXeFlashFileSystemRoot/CXeFlashBlockDriver:
 * each 0x4000-byte little block holds blockmap on even 0x200 pages
 * (0x100 big-endian WORDs each) and file entries on odd pages
 * (0x10 entries of 0x20 bytes each, big-endian fields).
 *
 * NAND is RAW data with spare/ECD, exactly what libxenon's sfcx raw
 * reads return. FS discovery uses spare sequence numbers (highest
 * wins); unmapped blocks are hard read errors, never silent 0xFF.
 * eMMC is flat logical data with Corona FS slots.
 */
#include "flashfs_priv.h"
int flashfs_mount(flashfs_t *fs, const uint8_t *img, size_t len,
                  flashfs_type_t type, uint16_t root)
{
    if (!fs || !img || len < FLASHFS_BLOCK_LEN)
        return -1;
    flashfs_unmount(fs); /* remount-safe: drops previous tables */
    fs->img = img;
    fs->len = len;
    fs->type = type;
    fs->fs_offset = 0;
    if (type == FLASHFS_EMMC) {
        fs->nblocks = (uint32_t)(len / FLASHFS_BLOCK_LEN);
        fs->blockmap = malloc((size_t)fs->nblocks * sizeof(uint16_t));
        fs->entries =
            malloc((size_t)fs->nblocks * sizeof(flashfs_entry_t));
        if (!fs->blockmap || !fs->entries) {
            flashfs_unmount(fs);
            return -1;
        }
        memset(fs->blockmap, 0xfe,
               (size_t)fs->nblocks * sizeof(uint16_t));
        return mount_root(fs, root, 0);
    }
    {
        fs_candidate_t cands[MAX_CANDIDATES];
        fs->backend = &flashfs_emu_backend;
        if (emu_load_geom(fs) != 0)
            return -1; /* unknown geometry */
        if (nand_alloc(fs) != 0)
            return -1;
        nand_scan_map(fs, cands, MAX_CANDIDATES); /* map only */
        return mount_root(fs, root, (int)root_version(fs, root));
    }
}

int flashfs_scan(flashfs_t *fs, const uint8_t *img, size_t len,
                 flashfs_type_t type)
{
    if (!fs || !img || len < FLASHFS_BLOCK_LEN)
        return -1;
    flashfs_unmount(fs); /* remount-safe: drops previous tables */
    fs->img = img;
    fs->len = len;
    fs->type = type;
    fs->fs_offset = 0;

    if (type == FLASHFS_EMMC) {
        uint32_t b;
        int best_ver = 0;
        uint16_t best = 0;
        fs->nblocks = (uint32_t)(len / FLASHFS_BLOCK_LEN);
        if (len < (size_t)CORONA_FS_ADDR + 0x8000u)
            return -1;
        fs->blockmap = malloc((size_t)fs->nblocks * sizeof(uint16_t));
        fs->entries =
            malloc((size_t)fs->nblocks * sizeof(flashfs_entry_t));
        if (!fs->blockmap || !fs->entries) {
            flashfs_unmount(fs);
            return -1;
        }
        memset(fs->blockmap, 0xfe,
               (size_t)fs->nblocks * sizeof(uint16_t));
        for (b = 0; b < 2; b++) {
            uint8_t slot[0x20];
            int ver;
            uint16_t blk;
            size_t off = (size_t)CORONA_FS_ADDR + (size_t)b * 0x4000u;
            if (off + sizeof(slot) > fs->len)
                continue;
            memcpy(slot, fs->img + off, sizeof(slot));
            ver = (int)rd32be(slot + 0x18);
            blk = rd16be(slot + 0x1c);
            if (ver > 0 && ver < 0x7fffffff && ver > best_ver &&
                blk < fs->nblocks) {
                best_ver = ver;
                best = blk;
            }
        }
        if (best_ver == 0) {
            flashfs_unmount(fs);
            return -1;
        }
        return mount_root(fs, best, best_ver);
    }
    {
        fs_candidate_t cands[MAX_CANDIDATES];
        uint32_t ncands, version = 0, root;
        fs->backend = &flashfs_emu_backend;
        if (emu_load_geom(fs) != 0)
            return -1;
        if (nand_alloc(fs) != 0)
            return -1;
        ncands = nand_scan_map(fs, cands, MAX_CANDIDATES);
        root = nand_pick(fs, cands, ncands, &version);
        if (root == 0xFFFFu) {
            flashfs_unmount(fs);
            return -1;
        }
        return mount_root(fs, (uint16_t)root, (int)version);
    }
}

void flashfs_unmount(flashfs_t *fs)
{
    if (!fs)
        return;
    free(fs->lba2phys);
    free(fs->blockmap);
    free(fs->entries);
    memset(fs, 0, sizeof(*fs));
}

uint32_t flashfs_count(const flashfs_t *fs)
{
    return fs ? fs->nentries : 0;
}

const flashfs_entry_t *flashfs_get(const flashfs_t *fs, uint32_t index)
{
    if (!fs || index >= fs->nentries)
        return NULL;
    return &fs->entries[index];
}

const flashfs_entry_t *flashfs_find(const flashfs_t *fs, const char *name)
{
    uint32_t i;
    if (!fs || !name)
        return NULL;
    for (i = 0; i < fs->nentries; i++) {
        if (strncmp(fs->entries[i].name, name, FLASHFS_NAME_LEN + 1) == 0)
            return &fs->entries[i];
    }
    return NULL;
}

/* Walk the block chain of `e`, copying file bytes [off, off+len). */
int flashfs_read(const flashfs_t *fs, const flashfs_entry_t *e,
                 uint8_t *out, uint32_t off, uint32_t len)
{
    static uint8_t blk[FLASHFS_BLOCK_LEN];
    uint32_t cur, skip, total = 0, guard = 0;
    if (!fs || !e || !out)
        return -1;
    if (off >= e->size)
        return 0;
    if (off + len > e->size)
        len = e->size - off;
    cur = e->block;
    skip = off;
    while (len > 0 && guard++ < fs->nblocks) {
        uint32_t blkidx = cur + fs->fs_offset;
        uint32_t cp, start = 0;
        if (cur >= fs->nblocks)
            return -1;
        if (block_read(fs, blkidx, blk, sizeof(blk)) != 0)
            return -1;
        if (skip >= FLASHFS_BLOCK_LEN) {
            skip -= FLASHFS_BLOCK_LEN;
        } else {
            start = skip;
            skip = 0;
            cp = FLASHFS_BLOCK_LEN - start;
            if (cp > len)
                cp = len;
            memcpy(out + total, blk + start, cp);
            total += cp;
            len -= cp;
        }
        cur = fs->blockmap[cur] & 0x7fffu;
        if (cur == FLASHFS_BLK_FREE || cur == FLASHFS_BLK_END)
            break;
    }
    return (int)total;
}
