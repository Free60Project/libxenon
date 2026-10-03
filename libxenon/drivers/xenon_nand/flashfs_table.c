/* flashfs_table.c - filetable parse, sanity filter, root chain, offset detect */
#include "flashfs_priv.h"

/* Parse one root block: append blockmap words + entries. Stops entry
 * scan at first empty/erased slot. Returns next root block in the
 * chain, or >=0x1ffb when the chain ends. */
static uint32_t root_parse(flashfs_t *fs, uint32_t blk, uint32_t map_base,
                           uint32_t *nentries)
{
    static uint8_t data[FLASHFS_BLOCK_LEN];
    uint32_t p, blks = FLASHFS_BLOCK_LEN / FLASHFS_PAGE_LEN / 2;

    if (_flashfs_block_read(fs, blk, data, sizeof(data)) != 0)
        return FLASHFS_BLK_END;
    for (p = 0; p < blks; p++) { /* even pages: blockmap */
        const uint8_t *pg = data + (p * 2) * FLASHFS_PAGE_LEN;
        uint32_t y;
        for (y = 0; y < 0x100; y++) {
            uint32_t idx = map_base + p * 0x100u + y;
            if (idx < fs->nblocks)
                fs->blockmap[idx] = rd16be(pg + y * 2);
        }
    }
    for (p = 0; p < blks; p++) { /* odd pages: entries */
        const uint8_t *pg = data + (p * 2 + 1) * FLASHFS_PAGE_LEN;
        uint32_t y;
        for (y = 0; y < 0x10; y++) {
            const uint8_t *e = pg + y * FLASHFS_ENTRY_LEN;
            uint16_t b = rd16be(e + FLASHFS_NAME_LEN);
            uint32_t sz = rd32be(e + FLASHFS_NAME_LEN + 2);
            uint32_t ts = rd32be(e + FLASHFS_NAME_LEN + 6);
            flashfs_entry_t *dst;
            /* Empty (name[0]==0) or erased (0xFFFF) slots end the table.
             * Block 0 is a valid start block, so only name/marker/size
             * decide. Zero-length files stay unrepresentable. */
            if (e[0] == 0 || b == 0xffffu || sz == 0)
                return fs->blockmap[blk] & 0x7fffu;
            if (*nentries >= fs->nblocks)
                return FLASHFS_BLK_END; /* insane image, stop */
            dst = &fs->entries[*nentries];
            memcpy(dst->name, e, FLASHFS_NAME_LEN);
            dst->name[FLASHFS_NAME_LEN] = '\0';
            if (dst->name[0] == '\x05')
                dst->name[0] = '_';
            dst->block = b;
            dst->size = sz;
            dst->timestamp = ts;
            (*nentries)++;
        }
    }
    return fs->blockmap[blk] & 0x7fffu;
}

/* Does the first entries page of root `blk` hold >= 2 sane entries?
 * Corruption filter for spare-scan candidates (not a ranking). */
int _flashfs_table_sane(const flashfs_t *fs, uint32_t blk)
{
    static uint8_t rootblk[FLASHFS_BLOCK_LEN];
    const uint8_t *ep;
    uint32_t y, valid = 0;
    if (blk >= fs->nblocks)
        return 0;
    if (_flashfs_block_read(fs, blk, rootblk, sizeof(rootblk)) != 0)
        return 0;
    ep = rootblk + FLASHFS_PAGE_LEN;
    for (y = 0; y < 0x10; y++) {
        const uint8_t *e = ep + y * FLASHFS_ENTRY_LEN;
        uint16_t b = rd16be(e + FLASHFS_NAME_LEN);
        uint32_t sz = rd32be(e + FLASHFS_NAME_LEN + 2);
        uint32_t k;
        if (e[0] == 0 || b == 0xffffu || sz == 0)
            break;
        if (e[0] < 0x20 || e[0] > 0x7eu)
            return 0;
        for (k = 0; k < FLASHFS_NAME_LEN; k++) {
            if (e[k] != 0 && (e[k] < 0x20 || e[k] > 0x7eu))
                return 0;
        }
        if (b >= fs->nblocks || sz >= HW_WINDOW)
            return 0;
        valid++;
    }
    return valid >= 2;
}

/* Big-block NANDs store file data at chain + fs_offset. Probe the
 * known candidates using bootanim.xex (else first *.xex, else entry 0)
 * expecting an XEX magic. Tries 0 first, so small-block/flat images
 * keep offset 0. */
static void detect_fs_offset(flashfs_t *fs)
{
    static const uint16_t cands[] = {0, 0x2E0, 0xAE0};
    const flashfs_entry_t *e = NULL;
    uint32_t i;
    size_t c;
    for (i = 0; i < fs->nentries; i++)
        if (!strcmp(fs->entries[i].name, "bootanim.xex")) {
            e = &fs->entries[i];
            break;
        }
    if (!e)
        for (i = 0; i < fs->nentries; i++) {
            size_t l = strlen(fs->entries[i].name);
            if (l > 4 &&
                !strcmp(fs->entries[i].name + l - 4, ".xex")) {
                e = &fs->entries[i];
                break;
            }
        }
    if (!e && fs->nentries)
        e = &fs->entries[0];
    if (!e)
        return;
    for (c = 0; c < sizeof(cands) / sizeof(cands[0]); c++) {
        static uint8_t blk[FLASHFS_BLOCK_LEN];
        uint32_t blkidx = e->block + cands[c];
        if (blkidx >= fs->nblocks)
            continue;
        if (_flashfs_block_read(fs, blkidx, blk, sizeof(blk)) != 0)
            continue;
        if (blk[0] == 'X' && blk[1] == 'E' && blk[2] == 'X') {
            fs->fs_offset = cands[c];
            return;
        }
    }
}

/* Load the filesystem rooted at little block `root`: follow the root chain
 * collecting blockmap words and entries, then detect the data offset. */
int _flashfs_mount_root(flashfs_t *fs, uint16_t root)
{
    uint32_t nentries = 0, next = root, guard = 0, map_base = 0;

    if (root >= fs->nblocks)
        return -1;
    /* follow root chain (multi-block roots append maps/entries).
     * A 0 link is an unmanaged map slot, not a chain to block 0
     * (header lives there; roots chain forward) — stop on it. */
    while (next != 0 && next < FLASHFS_BLK_RSRVD &&
           guard++ < fs->nblocks) {
        next = root_parse(fs, next, map_base, &nentries);
        map_base += 16u * 0x100u; /* 16 blockmap pages per root block */
    }
    fs->nentries = nentries;
    detect_fs_offset(fs);
    return 0;
}
