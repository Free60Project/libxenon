/*
 * flashfs.c - newlib `flash:` device over the Xbox 360 flash filesystem
 *
 * The filesystem is flat (no subdirectories) and read-only. Each 0x4000-byte
 * little block holds a blockmap on its even 0x200 pages and file entries on
 * its odd pages. NAND is read raw with spare data, which tells us which
 * physical block holds each LBA and which root is newest; eMMC is flat
 * logical data with two root pointers at CORONA_FS_ADDR.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <strings.h>
#include <sys/iosupport.h>
#include "flashfs.h"
#include "flashfs_priv.h"
#include "xb360/xb360.h"

typedef struct {
    const flashfs_entry_t *entry;
    uint32_t pos;
} file_t;

typedef struct {
    uint32_t index;
} dir_t;

/* One console has one flash, so a single mount is enough. */
static flashfs_t fs;
static devoptab_t devops;
static char devname[9];
static bool mounted;

void _flashfs_free(flashfs_t *f)
{
    free(f->lba2phys);
    free(f->blockmap);
    free(f->entries);
    memset(f, 0, sizeof(*f));
}

static int alloc_tables(flashfs_t *f)
{
    uint32_t b;
    f->blockmap = malloc((size_t)f->nblocks * sizeof(uint16_t));
    f->entries = malloc((size_t)f->nblocks * sizeof(flashfs_entry_t));
    if (!f->emmc) {
        f->lba2phys = malloc((size_t)f->phys_blocks * sizeof(uint16_t));
        for (b = 0; f->lba2phys && b < f->phys_blocks; b++)
            f->lba2phys[b] = NO_PHYS;
    }
    if (!f->blockmap || !f->entries || (!f->emmc && !f->lba2phys))
        return -1;
    memset(f->blockmap, 0xfe, (size_t)f->nblocks * sizeof(uint16_t));
    return 0;
}

/* Newest valid of the two eMMC root slots; 0xFFFF if neither is valid. */
static uint16_t emmc_root(const flashfs_t *f)
{
    uint32_t b;
    int best_ver = 0;
    uint16_t best = 0xFFFFu;
    for (b = 0; b < 2; b++) {
        uint8_t slot[0x20];
        int ver;
        uint16_t blk;
        if (xenon_get_logical_nand_data(slot, CORONA_FS_ADDR + b * 0x4000u,
                                        sizeof(slot)) != 0)
            continue;
        ver = (int)rd32be(slot + 0x18);
        blk = rd16be(slot + 0x1c);
        if (ver > best_ver && ver < 0x7fffffff && blk < f->nblocks) {
            best_ver = ver;
            best = blk;
        }
    }
    return best;
}

static int load_fs(flashfs_t *f)
{
    uint16_t root;

    if (xenon_is_emmc_console()) {
        f->emmc = 1;
        f->nblocks = MMC_FLASH_SIZE / FLASHFS_BLOCK_LEN;
        if (alloc_tables(f) != 0)
            return -1;
        root = emmc_root(f);
    } else {
        fs_candidate_t cands[MAX_CANDIDATES];
        if (_flashfs_load_geom(f) != 0 || alloc_tables(f) != 0)
            return -1;
        root = (uint16_t)_flashfs_nand_pick(
            f, cands, _flashfs_nand_scan(f, cands, MAX_CANDIDATES));
    }
    return _flashfs_mount_root(f, root); /* rejects the 0xFFFF "none" root */
}

/* Entry for a device path ("flash:/name"); NULL for the root or a miss. */
static const flashfs_entry_t *lookup(const char *path)
{
    uint32_t i;
    const char *colon = strchr(path, ':');
    if (colon)
        path = colon + 1;
    while (*path == '/')
        path++;
    for (i = 0; *path && i < fs.nentries; i++)
        if (strcasecmp(fs.entries[i].name, path) == 0)
            return &fs.entries[i];
    return NULL;
}

static bool is_root(const char *path)
{
    const char *colon = strchr(path, ':');
    if (colon)
        path = colon + 1;
    while (*path == '/')
        path++;
    return *path == '\0';
}

static void stat_file(const flashfs_entry_t *e, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0444;
    st->st_size = e->size;
    st->st_blksize = FLASHFS_BLOCK_LEN;
}

static void stat_root(struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0555;
}

static int ffs_open_r(struct _reent *r, void *fileStruct, const char *path,
                      int flags, int mode)
{
    file_t *file = fileStruct;
    if ((flags & O_ACCMODE) != O_RDONLY) {
        r->_errno = EROFS;
        return -1;
    }
    file->entry = lookup(path);
    if (!file->entry) {
        r->_errno = is_root(path) ? EISDIR : ENOENT;
        return -1;
    }
    file->pos = 0;
    return (int)file;
}

static int ffs_close_r(struct _reent *r, int fd)
{
    return 0;
}

static ssize_t ffs_read_r(struct _reent *r, int fd, char *ptr, size_t len)
{
    file_t *file = (file_t *)fd;
    int n = _flashfs_read(&fs, file->entry, (uint8_t *)ptr, file->pos, len);
    if (n < 0) {
        r->_errno = EIO;
        return -1;
    }
    file->pos += n;
    return n;
}

static off_t ffs_seek_r(struct _reent *r, int fd, off_t pos, int dir)
{
    file_t *file = (file_t *)fd;
    off_t base = dir == SEEK_CUR ? file->pos
               : dir == SEEK_END ? file->entry->size : 0;
    if ((dir != SEEK_SET && dir != SEEK_CUR && dir != SEEK_END) ||
        base + pos < 0) {
        r->_errno = EINVAL;
        return -1;
    }
    file->pos = base + pos;
    return file->pos;
}

static int ffs_fstat_r(struct _reent *r, int fd, struct stat *st)
{
    stat_file(((file_t *)fd)->entry, st);
    return 0;
}

static int ffs_stat_r(struct _reent *r, const char *path, struct stat *st)
{
    const flashfs_entry_t *e = lookup(path);
    if (e)
        stat_file(e, st);
    else if (is_root(path))
        stat_root(st);
    else {
        r->_errno = ENOENT;
        return -1;
    }
    return 0;
}

static DIR_ITER *ffs_diropen_r(struct _reent *r, DIR_ITER *dirState,
                               const char *path)
{
    if (!is_root(path)) {
        r->_errno = lookup(path) ? ENOTDIR : ENOENT;
        return NULL;
    }
    ((dir_t *)dirState->dirStruct)->index = 0;
    return dirState;
}

static int ffs_dirreset_r(struct _reent *r, DIR_ITER *dirState)
{
    ((dir_t *)dirState->dirStruct)->index = 0;
    return 0;
}

/* End of directory is signalled with ENOENT, as readdir() expects. */
static int ffs_dirnext_r(struct _reent *r, DIR_ITER *dirState,
                         char *filename, struct stat *st)
{
    dir_t *dir = dirState->dirStruct;
    if (dir->index >= fs.nentries) {
        r->_errno = ENOENT;
        return -1;
    }
    strcpy(filename, fs.entries[dir->index].name);
    stat_file(&fs.entries[dir->index++], st);
    return 0;
}

static int ffs_dirclose_r(struct _reent *r, DIR_ITER *dirState)
{
    return 0;
}

static int ffs_statvfs_r(struct _reent *r, const char *path,
                         struct statvfs *buf)
{
    memset(buf, 0, sizeof(*buf));
    buf->f_bsize = buf->f_frsize = FLASHFS_BLOCK_LEN;
    buf->f_blocks = fs.nblocks;
    buf->f_files = fs.nentries;
    buf->f_flag = ST_NOSUID | ST_RDONLY;
    buf->f_namemax = FLASHFS_NAME_LEN;
    return 0;
}

static const devoptab_t devops_template = {
    .structSize = sizeof(file_t),
    .open_r = ffs_open_r,
    .close_r = ffs_close_r,
    .read_r = ffs_read_r,
    .seek_r = ffs_seek_r,
    .fstat_r = ffs_fstat_r,
    .stat_r = ffs_stat_r,
    .dirStateSize = sizeof(dir_t),
    .diropen_r = ffs_diropen_r,
    .dirreset_r = ffs_dirreset_r,
    .dirnext_r = ffs_dirnext_r,
    .dirclose_r = ffs_dirclose_r,
    .statvfs_r = ffs_statvfs_r,
};

bool _flashfs_mount(const char *name)
{
    char dev[sizeof(devname) + 1];

    if (mounted || !name || !*name || strlen(name) >= sizeof(devname))
        return false;
    snprintf(dev, sizeof(dev), "%s:", name);
    if (FindDevice(dev) >= 0)
        return false;
    if (load_fs(&fs) != 0) {
        _flashfs_free(&fs);
        return false;
    }
    strcpy(devname, name);
    devops = devops_template;
    devops.name = devname;
    if (AddDevice(&devops) < 0) {
        _flashfs_free(&fs);
        return false;
    }
    mounted = true;
    return true;
}

bool _flashfs_unmount(const char *name)
{
    char dev[sizeof(devname) + 1];

    snprintf(dev, sizeof(dev), "%s:", name ? name : "");
    if (!mounted || strncmp(dev, devname, strlen(devname)) != 0 ||
        dev[strlen(devname)] != ':' || RemoveDevice(dev) < 0)
        return false;
    _flashfs_free(&fs);
    mounted = false;
    return true;
}

bool flashfs_mount()
{
    return _flashfs_mount("flash");
}

bool flashfs_unmount()
{
    return _flashfs_unmount("flash");
}
