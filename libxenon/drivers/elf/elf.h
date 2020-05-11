#ifndef ELF_H
#define	ELF_H

#ifdef	__cplusplus
extern "C" {
#endif

char *argv_GetFilename(const char *argv);
char *argv_GetFilepath(const char *argv);
char *argv_GetDevice(const char *argv);
void elf_setArgcArgv(int argc, char *argv[]);
int elf_runFromMemory(void *addr, int size);
int elf_runFromDisk(char *filename);
int elf_runWithDeviceTree(void *elf_addr, int elf_size, void *dt_addr,
                          int dt_size);
int kernel_prepare_initrd(void *start, size_t size);
void kernel_relocate_initrd(void *start, size_t size);
void kernel_reset_initrd(void);
void kernel_build_cmdline(const char *parameters, const char *root);

#ifdef	__cplusplus
}
#endif

#endif	/* ELF_H */

