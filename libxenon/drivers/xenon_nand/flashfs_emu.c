/* flashfs_emu.c - emulated-image backend: reads from a memory dump */
#include "flashfs_priv.h"

static int emu_subpage(const flashfs_t *fs, uint32_t g, uint8_t *data,
                       uint8_t *spare)
{
    uint32_t page_idx = (g * 512u) / fs->geom_page_data;
    uint32_t inpage = (g * 512u) % fs->geom_page_data;
    size_t src;
    if (!fs->img)
        return -1;
    src = (size_t)page_idx * page_real(fs);
    if (src + page_real(fs) > fs->len)
        return -1;
    if (data)
        memcpy(data, fs->img + src + inpage, 512);
    if (spare)
        memcpy(spare, fs->img + src + fs->geom_page_data,
               fs->geom_page_spare);
    return 0;
}

const flashfs_backend_t flashfs_emu_backend = { emu_subpage };

static const nand_geom_t nand_geoms[] = {
    {512, 16, 32, SPARE_SB},    /* small block */
    {512, 16, 32, SPARE_JASPER},/* Jasper small block */
    {512, 16, 256, SPARE_BB},   /* big block, small pages */
    {2048, 64, 64, SPARE_BB},   /* big block, large pages */
};

/* Detect geometry by trial: whole blocks; block 0/1 spares must decode
 * to LBA 0/1, good, non-erased. */
int emu_load_geom(flashfs_t *fs)
{
    size_t i;
    for (i = 0; i < sizeof(nand_geoms) / sizeof(nand_geoms[0]); i++) {
        fs->geom_page_data = nand_geoms[i].page_data;
        fs->geom_page_spare = nand_geoms[i].page_spare;
        fs->geom_pages_block = nand_geoms[i].pages_per_block;
        fs->geom_spare = nand_geoms[i].spare;
        if (geom_valid(fs, &nand_geoms[i])) {
            fs->lils_per_block =
                (fs->geom_pages_block * fs->geom_page_data) /
                FLASHFS_BLOCK_LEN;
            fs->phys_blocks =
                (uint32_t)(fs->len / block_real(fs));
            fs->nblocks = fs->phys_blocks * fs->lils_per_block;
            return 0;
        }
    }
    return -1; /* unknown geometry */
}
