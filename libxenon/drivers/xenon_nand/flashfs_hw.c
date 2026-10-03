/* flashfs_hw.c - live-flash backends for libxenon (NAND + eMMC).
 * Compiled for xenon targets only; the host build excludes this file,
 * so no feature guards are needed here. NAND reads go through
 * sfcx_read_page(raw=1); geometry and spare layout come from struct
 * sfc (sfcx_init runs lazily). eMMC reads go through
 * xenon_get_logical_nand_data (flat logical data, Corona slots). */
#include "flashfs_priv.h"
#include "xenon_nand/xenon_sfcx.h"
#include "xb360/xb360.h"

static int hw_subpage(const flashfs_t *fs, uint32_t g, uint8_t *data,
                      uint8_t *spare)
{
    static uint8_t raw[0x880];
    unsigned addr = (g * 512u / (uint32_t)sfc.page_sz) *
                    (uint32_t)sfc.page_sz;
    (void)fs;
    if (sfc.page_sz != 512 || (g * 512u) % (uint32_t)sfc.page_sz != 0)
        return -1; /* sfc exposes 512B pages only */
    if (sfcx_read_page(raw, (int)addr, 1) < 0)
        return -1;
    if (data)
        memcpy(data, raw, 512);
    if (spare)
        memcpy(spare, raw + sfc.page_sz, (size_t)sfc.meta_sz);
    return 0;
}

const flashfs_backend_t flashfs_hw_backend = { hw_subpage };

/* Live eMMC: flat 512B logical subpages (spare unused, reads as 0xFF). */
static int hw_emmc_subpage(const flashfs_t *fs, uint32_t g, uint8_t *data,
                           uint8_t *spare)
{
    (void)fs;
    if (data &&
        xenon_get_logical_nand_data(data, g * 512u, 512) != 0)
        return -1;
    if (spare)
        memset(spare, 0xFF, 64);
    return 0;
}

const flashfs_backend_t flashfs_hw_emmc_backend = { hw_emmc_subpage };

static int hw_emmc_setup(flashfs_t *fs)
{
    flashfs_unmount(fs);
    fs->img = NULL;
    fs->backend = &flashfs_hw_emmc_backend;
    fs->len = MMC_FLASH_SIZE;
    fs->type = FLASHFS_EMMC;
    fs->fs_offset = 0;
    fs->nblocks = (uint32_t)(fs->len / FLASHFS_BLOCK_LEN);
    fs->blockmap = malloc((size_t)fs->nblocks * sizeof(uint16_t));
    fs->entries =
        malloc((size_t)fs->nblocks * sizeof(flashfs_entry_t));
    if (!fs->blockmap || !fs->entries) {
        flashfs_unmount(fs);
        return -1;
    }
    memset(fs->blockmap, 0xfe,
           (size_t)fs->nblocks * sizeof(uint16_t));
    return 0;
}

/* Newest Corona slot wins (mirrors flashfs_scan's eMMC path, live). */
static int hw_emmc_find_root(uint16_t *root, int *version)
{
    uint32_t b, nblocks = MMC_FLASH_SIZE / FLASHFS_BLOCK_LEN;
    int best_ver = 0;
    uint16_t best = 0;
    for (b = 0; b < 2; b++) {
        uint8_t slot[0x20];
        int ver;
        uint16_t blk;
        if (xenon_get_logical_nand_data(slot,
                CORONA_FS_ADDR + b * 0x4000u, sizeof(slot)) != 0)
            continue;
        ver = (int)rd32be(slot + 0x18);
        blk = rd16be(slot + 0x1c);
        if (ver > 0 && ver < 0x7fffffff && ver > best_ver &&
            blk < nblocks) {
            best_ver = ver;
            best = blk;
        }
    }
    if (best_ver == 0)
        return -1;
    *root = best;
    *version = best_ver;
    return 0;
}

static int hw_load_geom(flashfs_t *fs)
{
    if (xenon_is_emmc_console())
        return -1;
    if (sfc.initialized != SFCX_INITIALIZED) {
        sfcx_init();
        if (sfc.initialized != SFCX_INITIALIZED)
            return -1;
    }
    if (sfc.page_sz != 512 || sfc.block_sz % FLASHFS_BLOCK_LEN != 0)
        return -1;
    fs->geom_page_data = (uint32_t)sfc.page_sz;
    fs->geom_page_spare = (uint32_t)sfc.meta_sz;
    fs->geom_pages_block = (uint32_t)sfc.pages_in_block;
    if (sfc.meta_type == 0)
        fs->geom_spare = SPARE_SB;
    else if (sfc.meta_type == 1)
        fs->geom_spare =
            (sfc.pages_in_block == 32) ? SPARE_JASPER : SPARE_BB;
    else
        fs->geom_spare = SPARE_BB;
    fs->lils_per_block =
        (uint32_t)sfc.block_sz / (uint32_t)FLASHFS_BLOCK_LEN;
    if (fs->lils_per_block == 0)
        return -1;
    fs->phys_blocks = (uint32_t)sfc.size_blocks;
    fs->nblocks = fs->phys_blocks * fs->lils_per_block;
    return 0;
}

int flashfs_mount_hw(flashfs_t *fs, uint16_t root)
{
    fs_candidate_t cands[MAX_CANDIDATES];
    if (!fs)
        return -1;
    if (xenon_is_emmc_console()) {
        if (hw_emmc_setup(fs) != 0)
            return -1;
        if (mount_root(fs, root, 0) != 0) {
            flashfs_unmount(fs);
            return -1;
        }
        return 0;
    }
    flashfs_unmount(fs);
    fs->img = NULL;
    fs->backend = &flashfs_hw_backend;
    fs->len = HW_WINDOW;
    fs->type = FLASHFS_NAND;
    fs->fs_offset = 0;
    if (hw_load_geom(fs) != 0 || nand_alloc(fs) != 0) {
        flashfs_unmount(fs);
        return -1;
    }
    nand_scan_map(fs, cands, MAX_CANDIDATES); /* map only */
    return mount_root(fs, root, (int)root_version(fs, root));
}

int flashfs_scan_hw(flashfs_t *fs)
{
    fs_candidate_t cands[MAX_CANDIDATES];
    uint32_t ncands, version = 0, root;
    uint16_t emmc_root;
    int emmc_ver;
    if (!fs)
        return -1;
    if (xenon_is_emmc_console()) {
        if (hw_emmc_setup(fs) != 0)
            return -1;
        if (hw_emmc_find_root(&emmc_root, &emmc_ver) != 0) {
            flashfs_unmount(fs);
            return -1;
        }
        if (mount_root(fs, emmc_root, emmc_ver) != 0) {
            flashfs_unmount(fs);
            return -1;
        }
        return 0;
    }
    flashfs_unmount(fs);
    fs->img = NULL;
    fs->backend = &flashfs_hw_backend;
    fs->len = HW_WINDOW;
    fs->type = FLASHFS_NAND;
    fs->fs_offset = 0;
    if (hw_load_geom(fs) != 0 || nand_alloc(fs) != 0) {
        flashfs_unmount(fs);
        return -1;
    }
    ncands = nand_scan_map(fs, cands, MAX_CANDIDATES);
    root = nand_pick(fs, cands, ncands, &version);
    if (root == 0xFFFFu) {
        flashfs_unmount(fs);
        return -1;
    }
    return mount_root(fs, (uint16_t)root, (int)version);
}
