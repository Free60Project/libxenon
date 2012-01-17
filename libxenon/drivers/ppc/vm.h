#ifndef __drivers_ppc_vm_h
#define __drivers_ppc_vm_h

#include <stdint.h>

// 64K bytes user pages

#define VM_USER_PAGE_BITS 16
#define VM_USER_PAGE_SIZE (1<<VM_USER_PAGE_BITS)
#define VM_USER_PAGE_MASK (VM_USER_PAGE_SIZE-1)

#define VM_WIMG_CACHED 0x190
#define VM_WIMG_GUARDED 0x1B8
#define VM_WIMG_MODIFIER_READ_ONLY 3
#define VM_WIMG_CACHED_READ_ONLY (VM_WIMG_CACHED|VM_WIMG_MODIFIER_READ_ONLY)
#define VM_WIMG_GUARDED_READ_ONLY (VM_WIMG_GUARDED|VM_WIMG_MODIFIER_READ_ONLY)

typedef void* (*vm_segfault_handler_t)(int, void *,void *,int); // processor id, address of the op causing the segfault, accessed address, write?
																// return value is address of next op, NULL for no change

void vm_create_user_mapping(uint32_t virt_addr, uint64_t phys_addr, int size, int wimg);
void vm_destroy_user_mapping(uint32_t virt_addr, int size);
void vm_set_user_mapping_flags(uint32_t virt_addr, int size, int wimg);
void vm_set_user_mapping_segfault_handler(vm_segfault_handler_t handler);
uint32_t vm_is_address_valid(uint32_t virt_addr);

#endif
