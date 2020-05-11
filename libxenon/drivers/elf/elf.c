/*  devtree preparation & initrd handling

Copyright (C) 2010-2011  Hector Martin "marcan" <hector@marcansoft.com>

This code is licensed to you under the terms of the GNU GPL, version 2;
see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

#include <fcntl.h>
#include <libfdt/libfdt.h>
#include <nocfe/cfe.h>
#include <ppc/cache.h>
#include <ppc/register.h>
#include <ppc/timebase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time/time.h>
#include <unistd.h>
#include <xenon_soc/xenon_power.h>

#include "elf.h"
#include "elf_abi.h"
#include "xetypes.h"

#define ELF_DEVTREE_START ((void *)0x85FE0000)
#define ELF_DEVTREE_MAX_SIZE 0x00010000
#define MAX_CMDLINE_SIZE 255

#define INITRD_RELOC_START ((void *)0x86000000)
#define INITRD_MAX_SIZE (32 * 1024 * 1024) // 0x02000000

// Address where the elf loading code is relocated.
/* TODO: must keep this synced with lis %r4,0x86f8
   and lis %r4,0x06f8 in elf_hold_thread elf_run.S */
#define ELF_CODE_STACK_END ((void *)0x87F80000)
#define ELF_CODE_RELOC_START ((void *)0x87F80000)
#define ELF_CODE_MAX_SIZE 0x00080000

// Temporary buffer to hold loaded elf data.
#define ELF_TEMP_BEGIN ((void *)0x88000000)
#define ELF_MAX_SIZE 0x03000000

// ELF file relocation address.
#define ELF_DATA_RELOC_START ((void *)0x8B000000)
#define ELF_ARGV_BEGIN ((void *)0x8E000000)
#define ELF_GET_RELOCATED(x)                                                   \
  (ELF_CODE_RELOC_START + ((unsigned long)(x) - (unsigned long)elfldr_start))
#define ELF_GET_RELOCATED_REAL(x) (void*)((uint64_t)ELF_GET_RELOCATED(x) & 0x1FFFFFFF)

#define ELF_DEBUG 1

extern void shutdown_drivers();

// section start/end, defined by the compiler
extern unsigned char elfldr_start[], elfldr_end[], pagetable_end[];
extern void elf_call_real(uint64_t function, ...);
extern void elf_run(unsigned long entry, unsigned long devtree);
extern void elf_hold_thread();

extern volatile unsigned long elf_secondary_hold_addr;
extern volatile unsigned long elf_secondary_count;

static char bootargs[MAX_CMDLINE_SIZE];

static uint8_t *initrd_start = NULL;
static size_t initrd_size = 0;

static char _filename[256] = {0};
static char _filepath[256] = {0};
static char _device[256] = {0};

static inline __attribute__((always_inline)) void elf_putch(unsigned char c) {
  while (!((*(volatile uint32_t *)(0xea001000 + 0x18)) & 0x02000000))
    ;
  *(volatile uint32_t *)(0xea001000 + 0x14) = (c << 24) & 0xFF000000;
}

static inline __attribute__((always_inline)) void elf_puts(unsigned char *s) {
  while (*s)
    elf_putch(*s++);
}

static inline __attribute__((always_inline)) void
elf_int2hex(unsigned long long n, unsigned char w, unsigned char *str) {
  unsigned char i, d;

  str += w;
  *str = 0;

  for (i = 0; i < w; ++i) {
    d = (n >> (i << 2)) & 0x0f;
    *--str = (d <= 9) ? d + 48 : d + 55;
  };
}

static inline __attribute__((always_inline)) void elf_memset(unsigned char *dst,
                                                             int v, int l) {
  while (l--)
    *dst++ = v;
}

static inline __attribute__((always_inline)) void
elf_memcpy(unsigned char *dst, const unsigned char *src, int l) {
  while (l--)
    *dst++ = *src++;
}

#define LINESIZE 128
static inline __attribute__((always_inline)) void
elf_sync(volatile void *addr) {
  asm volatile("dcbst 0, %0" : : "b"(addr));
  asm volatile("sync");
  asm volatile("icbi 0, %0" : : "b"(addr));
  asm volatile("isync");
}

static inline __attribute__((always_inline)) void
elf_sync_before_exec(unsigned char *dst, int l) {
  dst = (unsigned char *)((unsigned long)dst & (~(LINESIZE - 1)));

  l += (LINESIZE - 1);
  l &= ~(LINESIZE - 1);

  while (l > 0) {
    elf_sync(dst);
    dst += LINESIZE;
    l -= LINESIZE;
  }
}

static inline __attribute__((always_inline)) void
elf_smc_send_message(const unsigned char *msg) {
  while ((read32(0xEA001000 + 0x84) & 4) == 0)
    ;

  write32(0xEA001000 + 0x84, 4);
  write32n(0xEA001000 + 0x80, *(uint32_t *)(msg + 0));
  write32n(0xEA001000 + 0x80, *(uint32_t *)(msg + 4));
  write32n(0xEA001000 + 0x80, *(uint32_t *)(msg + 8));
  write32n(0xEA001000 + 0x80, *(uint32_t *)(msg + 12));
  write32(0xEA001000 + 0x84, 0);
}

static inline __attribute__((always_inline)) void elf_smc_set_led(int override,
                                                                  int value) {
  uint8_t buf[16];
  elf_memset(buf, 0, 16);

  buf[0] = 0x99;
  buf[1] = override;
  buf[2] = value;

  elf_smc_send_message(buf);
}

// Stage 2 of prepare run: This function is called in real-mode.
static void
    __attribute__((section(".elfldr"), used, noreturn, flatten, optimize("O2")))
    elf_prepare_run_s2(void *self, uint32_t entry, int mem_size) {
  // GCC is allowed to optimize away writes to 0, so do a stupid trick.
  volatile void* volatile zero = 0x0;

  // Final copy, and synchronize icache. This is the point of no return.
  elf_memcpy(zero, (void*)((uint64_t)ELF_TEMP_BEGIN & 0x1FFFFFFF), mem_size);
  elf_sync_before_exec(zero, mem_size);

  // Send secondary threads into the elf.
  *(volatile unsigned long *)(ELF_GET_RELOCATED_REAL(
      &elf_secondary_hold_addr)) = entry + 0x60;

  // load the argv struct (if this isn't the kernel)
  if (entry != 0) {
    // disable argument for linux
    void *new_argv = (void*)(0x00000008 + entry);
    elf_memcpy(new_argv, (void*)((uint64_t)ELF_ARGV_BEGIN & 0x1FFFFFFF), sizeof(struct __argv));
    elf_sync_before_exec(new_argv, sizeof(struct __argv));
  }

  // call elf_run() in elf_run.S
  void (*call)(unsigned long, unsigned long) = ELF_GET_RELOCATED_REAL(elf_run);
  call(entry, ((uint64_t)ELF_DEVTREE_START) & 0x1fffffff);

  // This should never be reached.
  for (;;)
    ;
}

// optimize("O2") is required to prevent call to _savegpr, which would fail due
// to the relocation
// This function is RELOCATED! You CANNOT call regular functions from here!
static void
    __attribute__((section(".elfldr"), used, noreturn, flatten, optimize("O2")))
    elf_prepare_run(void *addr, uint32_t size) {
  Elf32_Ehdr *ehdr;
  Elf32_Shdr *shdr;
  Elf32_Phdr *phdr;
  int i;

  int mem_size = 0;
  uint32_t pagetable_size = (uint32_t)pagetable_end & 0x1FFFFFFF;

  ehdr = (Elf32_Ehdr *)addr;
  shdr = (Elf32_Shdr *)(addr + ehdr->e_shoff +
                        (ehdr->e_shstrndx * sizeof(Elf32_Shdr)));
  phdr = (Elf32_Phdr *)(addr + ehdr->e_phoff);

  for (i = 0; i < ehdr->e_phnum; ++i) {
#if ELF_DEBUG
    elf_smc_set_led(1, (i << 4) & 0xF0); // flash green LEDs
#endif

    phdr = (Elf32_Phdr *)(addr + ehdr->e_phoff + (i * sizeof(Elf32_Phdr)));

    // Track the high address (even for pages we don't load)
    if (phdr->p_paddr + phdr->p_memsz > mem_size) {
      mem_size = phdr->p_paddr + phdr->p_memsz;
    }

    if (phdr->p_type == PT_LOAD || phdr->p_type == PT_DYNAMIC ||
        phdr->p_type == PT_NOTE) {
      // Zero then copy into our temporary memory...
      elf_memset(ELF_TEMP_BEGIN + phdr->p_paddr, 0, phdr->p_memsz);
      elf_memcpy(ELF_TEMP_BEGIN + phdr->p_paddr, addr + phdr->p_offset,
                 phdr->p_filesz);
    }
  }

  if (mem_size == 0) {
    // Uh-oh, no loading occurred. We're boned.
    elf_smc_set_led(1, 0x0F);
    while (1)
      ;
  }

#if ELF_DEBUG
  elf_smc_set_led(1, 0x87); // 1 green 3 red
#endif

  void (*call_real)(void *, uint32_t, int) = ELF_GET_RELOCATED(elf_call_real);
  call_real(ELF_GET_RELOCATED_REAL(elf_prepare_run_s2), ehdr->e_entry,
            mem_size);

  // Call these just so they aren't optimized out (this isn't reached).
  elf_call_real(NULL);
  elf_prepare_run_s2(NULL, 0, 0);

  // This should never be reached.
  for (;;)
    ;
}

char *argv_GetFilename(const char *argv) {
  char *tmp;

  if (argv == NULL)
    return NULL;

  tmp = strrchr(argv, '/');

  if (tmp == NULL)
    return NULL;

  strcpy(_filename, tmp + 1);

  return _filename;
}

char *argv_GetFilepath(const char *argv) {
  char *tmp;

  if (argv == NULL)
    return NULL;

  tmp = strrchr(argv, '/');

  if (tmp == NULL)
    return NULL;

  strncpy(_filepath, argv, tmp - argv);

  return _filepath;
}

char *argv_GetDevice(const char *argv) {
  char *tmp;

  if (argv == NULL)
    return NULL;

  tmp = strrchr(argv, ':');

  if (tmp == NULL)
    return NULL;

  strncpy(_device, argv, tmp - argv);

  return _device;
}

void elf_setArgcArgv(int argc, char *argv[]) {
  int i;

  // create argv struct and initialize it
  struct __argv args;
  memset(&args, 0, sizeof(struct __argv));
  args.magic = ARGV_MAGIC;
  args.argc = argc;

  // set the start of the argv array
  args.argv = (char *)ELF_ARGV_BEGIN + sizeof(struct __argv);
  char *position = args.argv + (sizeof(char *) * argc);

  // copy all the argument strings
  for (i = 0; i < argc; i++) {
    strcpy(position, argv[i]);
    args.argv[i] = position;
    position += strlen(argv[i]);

    // be sure its null terminated
    strcpy(position, "\0");
    position++;
  }

  // move the struct to its final position
  memmove(ELF_ARGV_BEGIN, &args, sizeof(args));
  elf_sync_before_exec(ELF_ARGV_BEGIN, sizeof(args) + position);
}

static int elf_VerifyHeaders(void *addr, int size) {
  Elf32_Ehdr *ehdr = (Elf32_Ehdr *)addr;
  Elf32_Shdr *shdr;
  Elf32_Phdr *phdr;
  unsigned char *strtab = 0;

  if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
      ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3) {
    printf("ELF: Magic incorrect\n");
    return -1;
  }

  if (ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
    printf("ELF: Not a 32-bit file!\n Did you forget to convert it?\n");
    return -1;
  }

  shdr = (Elf32_Shdr *)(addr + ehdr->e_shoff +
                        (ehdr->e_shstrndx * sizeof(Elf32_Shdr)));
  phdr = (Elf32_Phdr *)(addr + ehdr->e_phoff);

  if (shdr->sh_type == SHT_STRTAB) {
    strtab = (unsigned char *)(addr + shdr->sh_offset);
  }

  for (int i = 0; i < ehdr->e_phnum; i++) {
    phdr = (Elf32_Phdr *)(addr + ehdr->e_phoff + (i * sizeof(Elf32_Phdr)));
    if (phdr->p_offset + phdr->p_filesz > size) {
      printf("ELF: Program header %d exceeds file size! (0x%.8X > 0x%.8X)\n",
             phdr->p_offset + phdr->p_filesz, size);
      return -1;
    }
  }

  return 0;
}

int elf_runFromMemory(void *addr, int size) {
  uint64_t start_time;
  volatile uint32_t *elf_secondary_count_ptr =
      ELF_GET_RELOCATED(&elf_secondary_count);
  int i;

  if (elf_VerifyHeaders(addr, size) != 0) {
    printf(" * Elf headers invalid, abort!\n", addr);
    return -1;
  }

  if (size >= ELF_MAX_SIZE) {
    printf(" * Elf is too large, abort! (size = 0x%.8X)\n", size);
    return -1;
  }

  if (elfldr_end - elfldr_start >= ELF_CODE_MAX_SIZE) {
    printf(" * Internal error: elfldr code is too large! (size = 0x%.8X)\n",
           elfldr_end - elfldr_start);
    return -1;
  }

  printf(" * Executing @ 0x%.8X size 0x%.8X...\n", addr, size);
  shutdown_drivers();

  // relocate our code
  memcpy(ELF_CODE_RELOC_START, elfldr_start, elfldr_end - elfldr_start);
  memicbi(ELF_CODE_RELOC_START, elfldr_end - elfldr_start);

  // relocate elf data
  memcpy(ELF_DATA_RELOC_START, addr, size);

#if ELF_DEBUG
  printf(" - Waiting for secondary threads...\n");
#endif

  // get all threads to be on hold in the relocated zone
  // TODO: timeout and reset
  *elf_secondary_count_ptr = 0;
  xenon_thread_startup();
  printf("  - ");
  for (i = 1; i < 6; ++i) {
    while (xenon_run_thread_task(i, NULL, ELF_GET_RELOCATED(elf_hold_thread)))
      ;

    while (*elf_secondary_count_ptr < i) {
      udelay(100);
    }

    console_clrline();
    printf("  - %d", i);
  }
  printf("\n");

#if ELF_DEBUG
  printf(" - elf_prepare_run\n");
#endif

  // call elf_prepare_run()
  void (*call)(void *, uint32_t) = ELF_GET_RELOCATED(elf_prepare_run);
  call(ELF_DATA_RELOC_START, size);

  // never called, make sure the function survives linking
  elf_prepare_run(NULL, 0);
  return 0;
}

int elf_runFromDisk(char *filename) {
  int f = open(filename, O_RDONLY);
  if (f < 0) {
    return f;
  }

  struct stat s;
  stat(filename, &s);

  int size = s.st_size;
  void *buf = malloc(size);

  int r = read(f, buf, size);
  if (r < 0) {
    close(f);
    return r;
  }

  elf_runFromMemory(buf, r);

  return 0;
}

int elf_runWithDeviceTree(void *elf_addr, int elf_size, void *dt_addr,
                          int dt_size) {
  int res, node;

  if (dt_size > ELF_DEVTREE_MAX_SIZE) {
    printf("[ELF loader] Device tree too big (> %d bytes) !\n",
           ELF_DEVTREE_MAX_SIZE);
    return -1;
  }
  memset(ELF_DEVTREE_START, 0, ELF_DEVTREE_MAX_SIZE);

  res = fdt_open_into(dt_addr, ELF_DEVTREE_START, ELF_DEVTREE_MAX_SIZE);
  if (res < 0) {
    printf(" ! fdt_open_into() failed\n");
    return res;
  }

  node = fdt_path_offset(ELF_DEVTREE_START, "/chosen");
  if (node < 0) {
    printf(" ! /chosen node not found in devtree\n");
    return node;
  }

  if (bootargs[0]) {
    res = fdt_setprop(ELF_DEVTREE_START, node, "bootargs", bootargs,
                      strlen(bootargs) + 1);
    if (res < 0) {
      printf(" ! couldn't set chosen.bootargs property\n");
      return res;
    }
  }

  if (initrd_start && initrd_size) {
    kernel_relocate_initrd(initrd_start, initrd_size);

    u64 start, end;
    start = (u32)PHYSADDR((u32)initrd_start);
    res = fdt_setprop(ELF_DEVTREE_START, node, "linux,initrd-start", &start,
                      sizeof(start));
    if (res < 0) {
      printf("couldn't set chosen.linux,initrd-start property\n");
      return res;
    }

    end = (u32)PHYSADDR(((u32)initrd_start + (u32)initrd_size));
    res = fdt_setprop(ELF_DEVTREE_START, node, "linux,initrd-end", &end,
                      sizeof(end));
    if (res < 0) {
      printf("couldn't set chosen.linux,initrd-end property\n");
      return res;
    }
    res = fdt_add_mem_rsv(ELF_DEVTREE_START, start, initrd_size);
    if (res < 0) {
      printf("couldn't add reservation for the initrd\n");
      return res;
    }
  }

  node = fdt_path_offset(ELF_DEVTREE_START, "/memory");
  if (node < 0) {
    printf(" ! /memory node not found in devtree\n");
    return node;
  }
  /*
          res = fdt_add_mem_rsv(ELF_DEVTREE_START, (uint64_t)ELF_DEVTREE_START,
     ELF_DEVTREE_MAX_SIZE); if (res < 0){ printf("couldn't add reservation for
     the devtree\n"); return;
          }
  */

  res = fdt_pack(ELF_DEVTREE_START);
  if (res < 0) {
    printf(" ! fdt_pack() failed\n");
    return res;
  }

  memdcbst(ELF_DEVTREE_START, ELF_DEVTREE_MAX_SIZE);
  printf(" * Device tree prepared\n");

  elf_runFromMemory(elf_addr, elf_size);

  return -1; // If this point is reached, elf execution failed
}

int kernel_prepare_initrd(void *start, size_t size) {
  if (size > INITRD_MAX_SIZE) {
    printf(" ! Initrd bigger than INITRD_MAX_SIZE (32 MB), Aborting!\n");
    return -1;
  }

  if (initrd_start != NULL)
    free(initrd_start);

  initrd_start = (uint8_t *)malloc(size);

  memcpy(initrd_start, start, size);
  initrd_size = size;
  return 0;
}

void kernel_relocate_initrd(void *start, size_t size) {
  printf(" * Relocating initrd...\n");

  memset(INITRD_RELOC_START, 0, INITRD_MAX_SIZE);
  memcpy(INITRD_RELOC_START, start, size);
  memdcbst(INITRD_RELOC_START, INITRD_MAX_SIZE);

  initrd_start = INITRD_RELOC_START;
  initrd_size = size;

  printf("Initrd at %p/0x%lx: %ld bytes (%ldKiB)\n", initrd_start,
         (u32)PHYSADDR((u32)initrd_start), initrd_size, initrd_size / 1024);
}

void kernel_reset_initrd(void) {
  if (initrd_start != NULL)
    free(initrd_start);

  initrd_start = NULL;
  initrd_size = 0;
}

void kernel_build_cmdline(const char *parameters, const char *root) {
  bootargs[0] = 0;

  if (root) {
    strlcat(bootargs, "root=", MAX_CMDLINE_SIZE);
    strlcat(bootargs, root, MAX_CMDLINE_SIZE);
    strlcat(bootargs, " ", MAX_CMDLINE_SIZE);
  }

  if (parameters)
    strlcat(bootargs, parameters, MAX_CMDLINE_SIZE);

  printf("Kernel command line: '%s'\n", bootargs);
}
