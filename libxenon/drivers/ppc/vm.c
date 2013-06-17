#include <ppc/vm.h>
#include <assert.h>
#include <stdio.h>

#include "register.h"
#include "elf/elf_abi.h"
#include "usb/tinyehci/ehci_types.h"

/* Goal is to provide a *simple* memory layout with the following features:
	-	addresses at 0 should pagefault,
	- memory should be continously mapped (cached),
	- we need access to PCI space (guarded),
	- we need access to 0x200_x_x cpu regs (guarded)
*/

uint32_t pagetable[] __attribute__ ((section (".pagetable"))) = {
	0, /* zero "page", should pagefault */
	0,
	(0x20000000000ULL >> 10) | VM_WIMG_GUARDED, // CPU stuff
	
    -1, -1, -1, -1, -1, // 1280MB user pages

	(0x00000000 >> 10) | VM_WIMG_CACHED,
	(0x10000000 >> 10) | VM_WIMG_CACHED,

	(0x00000000 >> 10) | VM_WIMG_GUARDED,
	(0x10000000 >> 10) | VM_WIMG_GUARDED,

	(0xc0000000 >> 10) | VM_WIMG_GUARDED, // Flash (at 0xc8000000)
	(0xd0000000 >> 10) | VM_WIMG_GUARDED, // PCI config space
	(0xe0000000 >> 10) | VM_WIMG_GUARDED, // PCI space
	0,
};

uint32_t userpagetable[1280*1024*1024/VM_USER_PAGE_SIZE] = {0};
vm_segfault_handler_t vm_segfault_handler=NULL;

static void vm_invalidate_tlb(uint32_t ea)
{
	asm volatile ("tlbiel %0,1"::"r"(ea));
	asm volatile ("ptesync");
	asm volatile ("eieio");
}

static int vm_common_check_get_idx(uint32_t virt_addr, int size)
{
	assert(!(virt_addr&VM_USER_PAGE_MASK));
	assert(!(size&VM_USER_PAGE_MASK));
	assert(virt_addr>=0x30000000 && virt_addr<0x80000000);

	return (virt_addr-0x30000000)>>VM_USER_PAGE_BITS;
}

void vm_create_user_mapping(uint32_t virt_addr, uint64_t phys_addr, int size, int wimg)
{
	assert(!(phys_addr&VM_USER_PAGE_MASK));
	
	int page_idx=vm_common_check_get_idx(virt_addr,size);
	int page_addr=phys_addr | wimg;	
	
	while (size)
	{
		userpagetable[page_idx]=page_addr;

		vm_invalidate_tlb(virt_addr);
		
		size-=VM_USER_PAGE_SIZE;
		++page_idx;
		page_addr+=VM_USER_PAGE_SIZE;
		virt_addr+=VM_USER_PAGE_SIZE;
	}
}

void vm_destroy_user_mapping(uint32_t virt_addr, int size)
{
	int page_idx=vm_common_check_get_idx(virt_addr,size);
	
	while (size)
	{
		userpagetable[page_idx]=0;
		
		vm_invalidate_tlb(virt_addr);
		
		size-=VM_USER_PAGE_SIZE;
		++page_idx;
		virt_addr+=VM_USER_PAGE_SIZE;
	}
}

void vm_set_user_mapping_flags(uint32_t virt_addr, int size, int wimg)
{
	int page_idx=vm_common_check_get_idx(virt_addr,size);
	
	while (size)
	{
		if(userpagetable[page_idx])
		{
			userpagetable[page_idx]&=~VM_USER_PAGE_MASK;
			userpagetable[page_idx]|=wimg;
			
			vm_invalidate_tlb(virt_addr);
		}
		
		size-=VM_USER_PAGE_SIZE;
		++page_idx;
		virt_addr+=VM_USER_PAGE_SIZE;
	}
}

void vm_set_user_mapping_segfault_handler(vm_segfault_handler_t handler)
{
	vm_segfault_handler=handler;
}
