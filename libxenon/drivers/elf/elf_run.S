.section ".elfldr"

#include <ppc/asm.h>
#include <ppc/xenonsprs.h>

// NOTE: All code in this file is relocated. In order to access any static data,
// you must use the offset register r2.

.globl elf_call_real
elf_call_real:
	/* r3 = real address */
	mtsrr0 	%r3

	/* Clear IR/DR and set 32bit mode */
	mfmsr	%r11
	li		%r12, 0x30
	andc	%r11, %r11, %r12
	rldicl 	%r11, %r11, 0, 1
	mtsrr1	%r11

	/* real-mode-ize the stack pointer */
	clrldi	%r1, %r1, 35

	/* Now branch to our real-mode function. */
	rfid

.globl elf_hold_thread
elf_hold_thread:
	// r2 = (ELF_CODE_RELOC_START - elfldr_start) (offset to reloc code)
	lis %r2, 0x87f8  // This must be ELF_CODE_RELOC_START@h
	lis %r3, elfldr_start@h
	ori %r3, %r3, elfldr_start@l
	sub %r2, %r2, %r3

	lis		%r4, elf_secondary_count@h
	ori		%r4, %r4, elf_secondary_count@l

	/* tell the loader we're here! */
1:	lwarx  %r3, %r2, %r4
	addi   %r3, %r3, 1
	stwcx. %r3, %r2, %r4
	bne-   1b

	lis		%r3, elf_secondary_hold_addr@h
	ori		%r3, %r3, elf_secondary_hold_addr@l
	add		%r4,%r3,%r2

2:
//	or %r1, %r1, %r1		/* low priority */
	lwz %r3, 0(%r4)
	cmpwi %r3, 0
	beq 2b

	/* Fall into elf_run. */
//	or %r2, %r2, %r2
	li %r4,0

.globl elf_run
elf_run:
	/* Save arguments for b64 */
	mr	%r31,%r3 // entrypoint
	mr	%r30,%r4 // device tree

	/* Load the relocated b64 routine and set it as the branch target */
	lis		%r4, 0x07F8  // This must be ELF_CODE_RELOC_START@h
	lis		%r3, b64@h
	ori		%r3, %r3, b64@l
	add		%r4,%r4,%r3
	lis		%r3, elfldr_start@h
	ori		%r3, %r3, elfldr_start@l
	sub		%r4,%r4,%r3
	mtsrr0	%r4

	/* Clear IR/DR and set 64bits mode */
	mfmsr	%r3
	li		%r4, 0x30
	andc	%r3, %r3, %r4
	lis		%r4, 0x8000
	rldicr	%r4, %r4, 32, 31
	or		%r3, %r3, %r4
	mtsrr1	%r3

	/* Return from interrupt because we're swapping to real mode */
	rfid

b64:
		/* invalidate all SLBs */
	isync
	slbia
	isync

		/* reset page sizes */
	mfspr %r3, hid6
	li %r4, 0xF
	sldi %r4, %r4, 44
	andc %r3, %r3, %r4
	li %r4, 0x8
	sldi %r4, %r4, 44
	or %r3, %r3, %r4
	mtspr hid6, %r3
	sync
	isync

		/* reset TLB reload */
	mfspr %r3, lpcr
	li %r4, 0x400
	andc %r3,%r3,%r4
	mtspr lpcr, %r3
	sync
	isync

		/* gogogo :) */
	mfspr	%r3, pir /* linux wants thread # in r3 for secondary threads */
	cmplwi	%r3,0
	bne 1f

	// We're the main thread!
	mr	%r3,%r30
1:
	mr	%r4,%r31
	li	%r5,0

	// Kernel entry:
	// r3 = DT physical pointer
	// r4 = physical pointer to the kernel
	
	// Entrypoint (from elf_run)
	mtctr %r31
	bctr
	trap

.globl elf_secondary_hold_addr
elf_secondary_hold_addr:
	.long 0

.globl elf_secondary_count
elf_secondary_count:
	.long 0