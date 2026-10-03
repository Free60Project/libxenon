#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xenos/xenos.h>
#include <console/console.h>
#include <xenon_nand/xenon_sfcx.h>

#include <xenon_nand/flashfs.h>

int main(void)
{
	xenos_init(VIDEO_MODE_AUTO);
	console_init();

	sfcx_init(); /* init flash hw (flashfs_scan_hw also does this lazily) */

	flashfs_t fs = {0}; /* zero-init required */
	const flashfs_entry_t *e;
	char magic[5] = {0};
	uint32_t i;

	/* NAND + eMMC (Corona/Winchester) both scan live */
	if (flashfs_scan_hw(&fs) != 0) {
		printf("no flash filesystem found\n");
		return 1;
	}
	printf("root 0x%x v%d, %u files\n",
	       fs.root, fs.version, flashfs_count(&fs));

	for (i = 0; i < flashfs_count(&fs); i++) {
		e = flashfs_get(&fs, i); /* NULL past the end */
		printf("  %-22s block 0x%x size 0x%x\n",
		       e->name, e->block, e->size);
	}

	e = flashfs_find(&fs, "dash.xex"); /* NULL when missing */
	if (e && flashfs_read(&fs, e, (uint8_t *)magic, 0, 4) == 4)
		printf("dash.xex magic: %.4s\n", magic); /* XEX2, ASCII */
	else
		printf("dash.xex not found\n");

	flashfs_unmount(&fs);
	return 0;
}
