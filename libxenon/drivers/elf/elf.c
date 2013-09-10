/*  devtree preparation & initrd handling

Copyright (C) 2010-2011  Hector Martin "marcan" <hector@marcansoft.com>

This code is licensed to you under the terms of the GNU GPL, version 2;
see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <xenon_soc/xenon_power.h>
#include <ppc/cache.h>
#include <ppc/timebase.h>
#include <time/time.h>
#include <libfdt/libfdt.h>
#include <nocfe/cfe.h>

#include "xetypes.h"
#include "elf.h"
#include "elf_abi.h"

#define INITRD_RELOC_START ((void*)0x85FE0000)
#define INITRD_MAX_SIZE (32*1024*1024)

#define ELF_DEVTREE_START ((void*)0x87FE0000)
#define ELF_DEVTREE_MAX_SIZE 0x10000
#define MAX_CMDLINE_SIZE 255

#define ELF_CODE_RELOC_START	((void*)0x87FF0000) /* TODO: must keep this synced with lis %r4,0x87ff and lis %r4,0x07ff in elf_run.S */
#define ELF_TEMP_BEGIN		((void*)0x87F80000)
#define ELF_DATA_RELOC_START	((void*)0x88000000)
#define ELF_ARGV_BEGIN		((void*)0x8A000000)
#define ELF_GET_RELOCATED(x) (ELF_CODE_RELOC_START+((unsigned long)(x)-(unsigned long)elfldr_start))

extern void shutdown_drivers();

extern unsigned char elfldr_start[], elfldr_end[], pagetable_end[];
extern void elf_run(unsigned long entry,unsigned long devtree);
extern void elf_hold_thread();

extern volatile unsigned long elf_secondary_hold_addr;

static char bootargs[MAX_CMDLINE_SIZE];

static uint8_t *initrd_start = NULL;
static size_t initrd_size = 0;

static char _filename[256] = {0};
static char _filepath[256] = {0};
static char _device[256] = {0};

static inline __attribute__((always_inline)) void elf_putch(unsigned char c)
{
	while (!((*(volatile uint32_t*)(0xea001000 + 0x18))&0x02000000));
	*(volatile uint32_t*)(0xea001000 + 0x14) = (c << 24) & 0xFF000000;
}

static inline __attribute__((always_inline)) void elf_puts(unsigned char *s)
{
	while(*s) elf_putch(*s++);
}

static inline __attribute__((always_inline)) void elf_int2hex(unsigned long long n,unsigned char w,unsigned char *str)
{
  unsigned char i,d;
  
  str+=w;
  *str=0;

  for(i=0;i<w;++i){
  	d= ( n >> (i << 2) ) & 0x0f;
    *--str= (d<=9)?d+48:d+55;
  };
}


static inline __attribute__((always_inline)) void elf_memset(unsigned char *dst, int v, int l)
{
	while (l--) *dst++ = v;
}

static inline __attribute__((always_inline)) void elf_memcpy(unsigned char *dst, const unsigned char *src, int l)
{
	while (l--)
		*dst++ = *src++;
}

#define LINESIZE 128
static inline __attribute__((always_inline)) void elf_sync(volatile void *addr)
{
	asm volatile ("dcbst 0, %0" : : "b" (addr));
	asm volatile ("sync");
	asm volatile ("icbi 0, %0" : : "b" (addr));
	asm volatile ("isync");
}
      
static inline __attribute__((always_inline)) void elf_sync_before_exec(unsigned char *dst, int l)
{
	dst=(unsigned char *)((unsigned long)dst&(~(LINESIZE-1)));
	
	l+=(LINESIZE-1);
	l&=~(LINESIZE-1);
	
	while (l > 0)
	{
		elf_sync(dst);
		dst += LINESIZE;
		l -= LINESIZE;
	}
}

// optimize("O2") is required to prevent call to _savegpr, which would fail due to the relocation
static void __attribute__ ((section (".elfldr"),noreturn,flatten,optimize("O2"))) elf_prepare_run (void *addr)
{
	unsigned char s[50];
	
	Elf32_Ehdr *ehdr;
	Elf32_Shdr *shdr;
	unsigned char *strtab = 0;
	unsigned long begin_size=(unsigned long)pagetable_end&0x7fffffff;
	int had_begin_overwrite=0;
	int i;

	ehdr = (Elf32_Ehdr *) addr;

	shdr = (Elf32_Shdr *) (addr + ehdr->e_shoff + (ehdr->e_shstrndx * sizeof (Elf32_Shdr)));

	if (shdr->sh_type == SHT_STRTAB)
		strtab = (unsigned char *) (addr + shdr->sh_offset);

	for (i = 0; i < ehdr->e_shnum; ++i)
	{
		shdr = (Elf32_Shdr *) (addr + ehdr->e_shoff +
				(i * sizeof (Elf32_Shdr)));

		if (!(shdr->sh_flags & SHF_ALLOC) || shdr->sh_size == 0)
			continue;

		elf_int2hex(shdr->sh_addr,8,s);
		elf_puts(s);
		elf_putch(' ');
		elf_int2hex(shdr->sh_size,8,s);
		elf_puts(s);
		elf_putch(' ');
		elf_puts(&strtab[shdr->sh_name]);
		elf_putch('\r');
		elf_putch('\n');

		void *target = (void*)(((unsigned long)0x80000000) | shdr->sh_addr);

		if (shdr->sh_type == SHT_NOBITS) {
			elf_memset (target, 0, shdr->sh_size);
			elf_sync_before_exec (target, shdr->sh_size);
		} else {
			unsigned char *tgt=target;
			unsigned char *off=(unsigned char *) addr + shdr->sh_offset;
			unsigned long siz=shdr->sh_size;
			
			// we can't overwrite exception code and page table, else tlb load wouldn't work anymore...
			if ((unsigned long)tgt < (unsigned long)pagetable_end) {
				
				if((((unsigned long)tgt + siz) < (unsigned long)pagetable_end) || had_begin_overwrite){
					// unhandled case...
					elf_putch('!');
					elf_putch('P');
					elf_putch('T');
					elf_putch('\r');
					elf_putch('\n');
					for(;;);
				}
				
				had_begin_overwrite=1;
				elf_memcpy(ELF_TEMP_BEGIN, off, begin_size);
				tgt=pagetable_end;
				siz=siz-begin_size;
				off=off+begin_size;
			}

			elf_memcpy (tgt, off, siz);
			elf_sync_before_exec (tgt, siz);
		}
	}

	if (had_begin_overwrite){
		//now we can overwrite it!
		elf_memcpy((void*)0x80000000, ELF_TEMP_BEGIN, begin_size);
		elf_sync_before_exec ((void*)0x80000000, begin_size);
	}
	
	elf_putch('\r');
	elf_putch('\n');
	elf_putch('E');
	elf_putch('P');
	elf_putch(' ');
	elf_int2hex(ehdr->e_entry,8,s);
	elf_puts(s);
	elf_putch('\r');
	elf_putch('\n');

	*(volatile unsigned long *)ELF_GET_RELOCATED(&elf_secondary_hold_addr) = ehdr->e_entry + 0x60;

	// load the argv struct
	if(ehdr->e_entry != 0){
		// disable argument for linux
		void *new_argv = 0x80000008 + ehdr->e_entry;
		elf_memcpy(new_argv, ELF_ARGV_BEGIN, sizeof(struct __argv));
		elf_sync_before_exec(new_argv, sizeof(struct __argv));
	}

	// call elf_run()
	void(*call)(unsigned long,unsigned long) = ELF_GET_RELOCATED(elf_run);
	call(ehdr->e_entry,((unsigned long)ELF_DEVTREE_START)&0x7fffffff);
	
	for(;;);
}

char *argv_GetFilename(const char *argv)
{
    char *tmp;
 
    if (argv == NULL) 
        return NULL;
 
    tmp = strrchr(argv, '/');
 
    if (tmp == NULL)
        return NULL;
 
    strcpy(_filename,tmp+1);
    
    return _filename;
}

char *argv_GetFilepath(const char *argv)
{
    char *tmp;
 
    if (argv == NULL) 
        return NULL;
  
    tmp = strrchr(argv, '/');
 
    if (tmp == NULL)
        return NULL;
 
 
    strncpy(_filepath,argv,tmp-argv);
    
    return _filepath;
}

char *argv_GetDevice(const char *argv)
{
    char *tmp;
 
    if (argv == NULL) 
        return NULL;
    
    tmp = strrchr(argv, ':');
 
    if (tmp == NULL)
        return NULL;
 
    strncpy(_device,argv,tmp-argv);
    
    return _device;
}

void elf_setArgcArgv(int argc, char *argv[])
{
	int i;

	// create argv struct and initialize it
	struct __argv args;
	memset(&args, 0, sizeof(struct __argv));
	args.magic = ARGV_MAGIC;
	args.argc = argc;

	// set the start of the argv array
	args.argv = (char*) ELF_ARGV_BEGIN + sizeof(struct __argv);
	char *position = args.argv + (sizeof(char*) * argc);

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

void elf_runFromMemory (void *addr, int size)
{
	int i;
	
    printf(" * Executing...\n");
	shutdown_drivers();
	
	// relocate code
	memcpy(ELF_CODE_RELOC_START,elfldr_start,elfldr_end-elfldr_start); 
	memicbi(ELF_CODE_RELOC_START,elfldr_end-elfldr_start);

	// relocate elf data
	memcpy(ELF_DATA_RELOC_START,addr,size);

	// get all threads to be on hold in the relocated zone
	xenon_thread_startup();
	for (i = 1; i < 6; ++i)
		while (xenon_run_thread_task(i, NULL, ELF_GET_RELOCATED(elf_hold_thread)));
		
	mdelay(200); // TODO: remove this and detect that all threads are on hold

	// call elf_prepare_run()
	void(*call)(void*) = ELF_GET_RELOCATED(elf_prepare_run);
	call(ELF_DATA_RELOC_START);
	
	// never called, make sure the function survives linking
	elf_prepare_run(NULL);
}

int elf_runFromDisk (char *filename)
{
	int f = open(filename, O_RDONLY);
	if (f < 0)
	{
		return f;
	}

	struct stat s;
	stat(filename, &s);

	int size = s.st_size;
	void * buf=malloc(size);

	int r = read(f, buf, size);
	if (r < 0)
	{
		close(f);
		return r;
	}

	elf_runFromMemory(buf, r);

	return 0;
}

int elf_runWithDeviceTree (void *elf_addr, int elf_size, void *dt_addr, int dt_size)
{
	int res, node;

	if (dt_size>ELF_DEVTREE_MAX_SIZE){
		printf("[ELF loader] Device tree too big (> %d bytes) !\n",ELF_DEVTREE_MAX_SIZE);
		return -1;
	}		
	memset(ELF_DEVTREE_START,0,ELF_DEVTREE_MAX_SIZE);

    res = fdt_open_into(dt_addr, ELF_DEVTREE_START, ELF_DEVTREE_MAX_SIZE);
	if (res < 0){
		printf(" ! fdt_open_into() failed\n"); 
        return res;
    }

	node = fdt_path_offset(ELF_DEVTREE_START, "/chosen");
	if (node < 0){
		printf(" ! /chosen node not found in devtree\n"); 
        return node;
    }

    if (bootargs[0])
    {
        res = fdt_setprop(ELF_DEVTREE_START, node, "bootargs", bootargs, strlen(bootargs)+1);
		if (res < 0){
			printf(" ! couldn't set chosen.bootargs property\n"); 
			return res;
		}
    }
	
    if (initrd_start && initrd_size)
    {
		kernel_relocate_initrd(initrd_start,initrd_size);
                
		u64 start, end;
		start = (u32)PHYSADDR((u32)initrd_start);
		res = fdt_setprop(ELF_DEVTREE_START, node, "linux,initrd-start", &start, sizeof(start));
		if (res < 0){
			printf("couldn't set chosen.linux,initrd-start property\n");
            return res;
        }

		end = (u32)PHYSADDR(((u32)initrd_start + (u32)initrd_size));
		res = fdt_setprop(ELF_DEVTREE_START, node, "linux,initrd-end", &end, sizeof(end));
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
	 if (node < 0){
		printf(" ! /memory node not found in devtree\n"); 
		return node;
     }
/*
	res = fdt_add_mem_rsv(ELF_DEVTREE_START, (uint64_t)ELF_DEVTREE_START, ELF_DEVTREE_MAX_SIZE);
	if (res < 0){
    	printf("couldn't add reservation for the devtree\n"); 
    	return;
	}
*/

	res = fdt_pack(ELF_DEVTREE_START);
	if (res < 0){
		printf(" ! fdt_pack() failed\n"); 
        return res;
    }
	
	memdcbst(ELF_DEVTREE_START,ELF_DEVTREE_MAX_SIZE);
	printf(" * Device tree prepared\n"); 
	
	elf_runFromMemory(elf_addr,elf_size);
	
	return -1; // If this point is reached, elf execution failed
}

int kernel_prepare_initrd(void *start, size_t size)
{       
	if (size > INITRD_MAX_SIZE){
		printf(" ! Initrd bigger than 32 MB, Aborting!\n");
		return -1;
	}
        
    if(initrd_start != NULL)
		free(initrd_start);
		
    initrd_start = (uint8_t*)malloc(size);
        
    memcpy(initrd_start,start,size);
    initrd_size = size;
    return 0;
}

void kernel_relocate_initrd(void *start, size_t size)
{       
	printf(" * Relocating initrd...\n");
        
	memset(INITRD_RELOC_START,0,INITRD_MAX_SIZE);
	memcpy(INITRD_RELOC_START,start,size);
	memdcbst(INITRD_RELOC_START,INITRD_MAX_SIZE);
        
	initrd_start = INITRD_RELOC_START;
	initrd_size = size;
        
	printf("Initrd at %p/0x%lx: %ld bytes (%ldKiB)\n", initrd_start, \
	(u32)PHYSADDR((u32)initrd_start), initrd_size, initrd_size/1024);
}

void kernel_reset_initrd(void)
{   
    if (initrd_start != NULL)
        free(initrd_start);
    
    initrd_start = NULL;
    initrd_size = 0;
}

void kernel_build_cmdline(const char *parameters, const char *root)
{
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
