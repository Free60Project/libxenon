/*
 * flashfs - read-only access to the Xbox 360 flash filesystem (NAND and eMMC)
 * through newlib, as a flat `flash:/` device.
 */
#ifndef _FLASHFS_H
#define _FLASHFS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Locate the newest filesystem on the console's flash and register it as the
 * newlib device `flash:/`, so files can be used with
 * fopen/open/opendir on "flash:/xboxkrnl.exe" or "flash:/".
 * Works on both NAND and eMMC consoles; initialises the flash controller if
 * needed. Only one instance can be mounted at a time.
 * Returns false if no filesystem was found, the name is taken, or on OOM.
 */
bool flashfs_mount(const char *name);

/* Unregister the device `flash:` and free the parsed tables. */
bool flashfs_unmount(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _FLASHFS_H */
