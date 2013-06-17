.section ".except"

#include <ppc/xenonsprs.h>

//#define DEBUG 1
#define PAGE_BITS 12

#define OFF_REG(r) r * 8
#define OFF_FPREG(r) (r+40) * 8
#define REG_LR 32
#define REG_CTR 33
#define REG_CR 34
#define REG_XER 35
#define REG_SRR0 36
#define REG_SRR1 37
#define REG_DAR 38
#define REG_PIR 39

#define HV_CONTEXT_SIZE 0x400

.extern pagetable

.globl exc_base
exc_base:

.globl secondary_lock
secondary_lock:
	.long 0

.globl secondary_alive
secondary_alive:
	.long 0

debug_lock:
	.long 0

#define DO_EXCEPT(addr, fnc) \
. = exc_base + addr;  \
mtsprg0 %r31; \
mfsprg1 %r31; \
std %r1, OFF_REG(1)(%r31); \
std %r2, OFF_REG(2)(%r31); \
std %r3, OFF_REG(3)(%r31); \
std %r4, OFF_REG(4)(%r31); \
li %r3, addr; \
b fnc

// DO_EXCEPT(0x00000100, ex_reset);

 . = exc_base + 0x100

mfspr   %r3, 0x3F1
li      %r4, 3
rldimi  %r3, %r4, 58,4
rldimi  %r3, %r4, 38,25
sync
mtspr   0x3F1, %r3
sync
isync
mflr %r7
mtsrr0  %r7
mtsrr1  %r8
rfid

mr %r2, %r2
blr

DO_EXCEPT(0x00000200, ex_machine_check);
DO_EXCEPT(0x00000300, ex_dsi);
DO_EXCEPT(0x00000380, ex_data_segment);
DO_EXCEPT(0x00000400, ex_isi);
DO_EXCEPT(0x00000480, ex_instruction_segment);

DO_EXCEPT(0x00000500, ex_interrupt);
DO_EXCEPT(0x00000600, ex_alignment);
DO_EXCEPT(0x00000700, ex_program);
DO_EXCEPT(0x00000800, ex_floating_point);

DO_EXCEPT(0x00000900, ex_decrementer);
DO_EXCEPT(0x00000C00, ex_system_call);
DO_EXCEPT(0x00000D00, ex_trace);
DO_EXCEPT(0x00000F00, ex_performance);

DO_EXCEPT(0x00001300, ex_iabr);
DO_EXCEPT(0x00001400, ex_reserved);
DO_EXCEPT(0x00001700, ex_thermal);

//1a8: 0000000000007f85 00000000ea0000ac 000000007f8xxxxx (l=1) -> 00000000ea000000, N=1, WIMG=5, pp=0, lp=0

ex_isi:
	std %r0, OFF_REG(0)(%r31)
	std %r5, OFF_REG(5)(%r31)
	std %r6, OFF_REG(6)(%r31)
	std %r7, OFF_REG(7)(%r31)
	std %r8, OFF_REG(8)(%r31)
	std %r9, OFF_REG(9)(%r31)
	std %r10, OFF_REG(10)(%r31)
	std %r11, OFF_REG(11)(%r31)
	std %r12, OFF_REG(12)(%r31)
	std %r13, OFF_REG(13)(%r31)
	std %r14, OFF_REG(14)(%r31)
	std %r15, OFF_REG(15)(%r31)
	std %r16, OFF_REG(16)(%r31)
	std %r17, OFF_REG(17)(%r31)
	std %r18, OFF_REG(18)(%r31)
	std %r19, OFF_REG(19)(%r31)
	std %r20, OFF_REG(20)(%r31)
	std %r21, OFF_REG(21)(%r31)
	std %r22, OFF_REG(22)(%r31)
	std %r23, OFF_REG(23)(%r31)
	std %r24, OFF_REG(24)(%r31)
	std %r25, OFF_REG(25)(%r31)
	std %r26, OFF_REG(26)(%r31)
	std %r27, OFF_REG(27)(%r31)
	std %r28, OFF_REG(28)(%r31)
	std %r29, OFF_REG(29)(%r31)
	std %r30, OFF_REG(30)(%r31)

	mfsprg0 %r4
	std %r4, OFF_REG(31)(%r31)
	mflr %r4
	std %r4, OFF_REG(REG_LR)(%r31)
	mfcr %r4
	std %r4, OFF_REG(REG_CR)(%r31)
	mfctr %r4
	std %r4, OFF_REG(REG_CTR)(%r31)
	mfxer %r4
	std %r4, OFF_REG(REG_XER)(%r31)

#if DEBUG
	mflr %r2
	li %r3, 'I'
	bl putc
	li %r3, ' '
	bl putc
	mtlr %r2
	li %r3, 's'
	bl putc
	li %r3, 'r'
	bl putc
	li %r3, 'r'
	bl putc
	li %r3, '0'
	bl putc
	li %r3, '='
	bl putc
	mfsrr0 %r3
	bl puthex
	li %r3, ' '
	bl putc
	li %r3, 'H'
	bl putc
	li %r3, 'i'
	bl putc
	li %r3, 'n'
	bl putc
	li %r3, 't'
	bl putc
	li %r3, '='
	bl putc
#endif

#if DEBUG
	mfspr %r3, PpeTlbIndexHint
	bl puthex
	li %r3, ','
	bl putc
#endif

	mfsrr0 %r3 // get faulting address

	rldicl %r4, %r3, 36, 60 // extract high nibble

	sldi %r4, %r4, 2
	addis %r4, %r4, pagetable@ha - 0x80000000
	lwz %r4, pagetable@l(%r4)

	cmpldi %r4, 0
	beq page_fault

	

	rldicl %r3, %r3, 0, 36 // extract rest of addr
	rldicr %r5, %r4, 10, 40
	rldicl %r6, %r4, 0, 50
	or %r3, %r3, %r5
		// r3 now contains the phys addr

	mfspr %r4, PpeTlbIndexHint

	rldicl %r2, %r3, 64-PAGE_BITS, 64-PAGE_BITS-1 // page nr
	rldicr %r2, %r2, 0, 47 // round down to 64K

	rldimi  %r4, %r2, 63-26, 16 //  put into bits 16..26

	mr %r15, %r4
#if DEBUG
	mr %r16, %r3

	li %r3, 'r'
	bl putc
	li %r3, '='
	bl putc
	mr %r3, %r16
1:
	bl puthex
	li %r3, ' '
	bl putc

	mr %r3, %r2
	bl puthex
	li %r3, ' '
	bl putc

	mr %r3, %r15
	bl puthex
	li %r3, ' '
	bl putc
	mr %r3, %r16
#endif

// 00001120_00000898
	mtspr PpeTlbIndex, %r15

		/* ARPN */
	rldicr %r2, %r3, 0, 47 // round down to 64K

	or %r2, %r2, %r6

	mfsrr0 %r15
///	mr %r15, %r3

#if DEBUG
	mr %r3, %r2
	bl puthex
	li %r3, ' '
	bl putc
#endif

	mtspr PpeTlbRpn, %r2

	rldicr %r2, %r15, 60 - PAGE_BITS, 56 // AVPN
	rldicl %r2, %r2, 0, 20 // whatever
	ori %r2, %r2, 5 // set valid
#if DEBUG
	mr %r3, %r2
	bl puthex
	li %r3, '\n'
	bl putc
#endif

// 00000000_00004481

	mtspr PpeTlbVpn, %r2
	b except_return


ex_dsi:

    std %r0, OFF_REG(0)(%r31)
	std %r5, OFF_REG(5)(%r31)
	std %r6, OFF_REG(6)(%r31)
	std %r7, OFF_REG(7)(%r31)
	std %r8, OFF_REG(8)(%r31)
	std %r9, OFF_REG(9)(%r31)
	std %r10, OFF_REG(10)(%r31)
	std %r11, OFF_REG(11)(%r31)
	std %r12, OFF_REG(12)(%r31)
	std %r13, OFF_REG(13)(%r31)
	std %r14, OFF_REG(14)(%r31)
	std %r15, OFF_REG(15)(%r31)
	std %r16, OFF_REG(16)(%r31)
	std %r17, OFF_REG(17)(%r31)
	std %r18, OFF_REG(18)(%r31)
	std %r19, OFF_REG(19)(%r31)
	std %r20, OFF_REG(20)(%r31)
	std %r21, OFF_REG(21)(%r31)
	std %r22, OFF_REG(22)(%r31)
	std %r23, OFF_REG(23)(%r31)
	std %r24, OFF_REG(24)(%r31)
	std %r25, OFF_REG(25)(%r31)
	std %r26, OFF_REG(26)(%r31)
	std %r27, OFF_REG(27)(%r31)
	std %r28, OFF_REG(28)(%r31)
	std %r29, OFF_REG(29)(%r31)
	std %r30, OFF_REG(30)(%r31)
	mfsprg0 %r4
	std %r4, OFF_REG(31)(%r31)
	mflr %r4
	std %r4, OFF_REG(REG_LR)(%r31)
	mfcr %r4
	std %r4, OFF_REG(REG_CR)(%r31)
	mfctr %r4
	std %r4, OFF_REG(REG_CTR)(%r31)
	mfxer %r4
	std %r4, OFF_REG(REG_XER)(%r31)

    /* dabr breakpoint */
    mfdsisr %r4
    rlwinm %r4,%r4,10,31,31
    cmplwi %r4,1
    beq dump_from_pagefault

#if DEBUG
	mflr %r2
	li %r3, 'D'
	bl putc
	li %r3, ' '
	bl putc
	mtlr %r2
	li %r3, 's'
	bl putc
	li %r3, 'r'
	bl putc
	li %r3, 'r'
	bl putc
	li %r3, '0'
	bl putc
	li %r3, '='
	bl putc
	mfsrr0 %r3
	bl puthex
	li %r3, ' '
	bl putc
	li %r3, 'D'
	bl putc
	li %r3, 'A'
	bl putc
	li %r3, 'R'
	bl putc
	li %r3, '='
	bl putc
	bl putc
	mfdar %r3 // get faulting address
	bl puthex
	li %r3, ' '
	bl putc
	li %r3, 'D'
	bl putc
	li %r3, 'S'
	bl putc
	li %r3, 'I'
	bl putc
	li %r3, 'S'
	bl putc
	li %r3, 'R'
	bl putc
	li %r3, '='
	bl putc
	mfspr %r3, 18 // dsisr
	bl puthex
	li %r3, ' '
	bl putc
	li %r3, 'H'
	bl putc
	li %r3, 'i'
	bl putc
	li %r3, 'n'
	bl putc
	li %r3, 't'
	bl putc
	li %r3, '='
	bl putc
#endif

#if DEBUG
	mfspr %r3, PpeTlbIndexHint
	bl puthex
	li %r3, ','
	bl putc
#endif

	mfdar %r3 // get faulting address

	rldicl %r4, %r3, 36, 60 // extract high nibble (right aligned)

	sldi %r4, %r4, 2 /* address pagetable */
	addis %r4, %r4, pagetable@h - 0x80000000 
	lwz %r4, pagetable@l(%r4) /* load physical addr & attribs */

	cmpldi %r4, 0
	beq page_fault

	cmpwi %r4, -1
	beq user_page_handler

	rldicl %r3, %r3, 0, 36  // &0x0FFFFFFF
	rldicr %r5, %r4, 10, 40 // shift left addr << 10
	rldicl %r6, %r4, 0, 50  // leave lower 14 bits of RPN
	or %r3, %r3, %r5        //r3 now contains the phys addr

	mfspr %r4, PpeTlbIndexHint

	rldicl %r2, %r3, 64-PAGE_BITS, 64-PAGE_BITS // page nr

	rldicr %r2, %r2, 0, 47 // round down to 64K

	rldimi  %r4, %r2, 63-26, 16 //  put into bits 16..26

	mr %r15, %r4  /* page number */
#if DEBUG
	mr %r16, %r3

	li %r3, 'r'
	bl putc
	li %r3, '='
	bl putc
	mr %r3, %r16
1:
	bl puthex
	li %r3, ' '
	bl putc

	mr %r3, %r2
	bl puthex
	li %r3, ' '
	bl putc

	mr %r3, %r15
	bl puthex
	li %r3, ' '
	bl putc
	mr %r3, %r16
#endif

// 00001120_00000898
	mtspr PpeTlbIndex, %r15

		/* ARPN */
	rldicr %r2, %r3, 0, 47 // round down to 64K

	or %r2, %r2, %r6 /* OR in page attributes */

	mfdar %r15
///	mr %r15, %r3

#if DEBUG
	mr %r3, %r2
	bl puthex
	li %r3, ' '
	bl putc
#endif

	mtspr PpeTlbRpn, %r2

			/* calculate VPN */

	rldicr %r2, %r15, 60 - PAGE_BITS, 56 // AVPN
	rldicl %r2, %r2, 0, 20 // whatever
	ori %r2, %r2, 5 // set V|L  (valid, large)
#if DEBUG
	mr %r3, %r2
	bl puthex
	li %r3, '\n'
	bl putc
#endif

// 00000000_00004481

	mtspr PpeTlbVpn, %r2
	
	b except_return

user_page_handler:
	mr %r15, %r3

	rldicl %r4,%r15,48,49 // calc idx in userpagetable
    subi %r4,%r4,0x3000
	sldi %r4,%r4,2

	addis %r4, %r4, userpagetable@ha - 0x80000000
	lwz %r4, userpagetable@l(%r4)

	mfdsisr %r6
	rldicl %r6,%r6,39,63

		/* no mapping -> segfault */
	cmplwi %r4,0
	beq user_segfault_handler

		/* if write in read only page, segfault too */
	rldicl %r5,%r4,0,62
	cmplwi %r5,3
	bne 1f
	cmplwi %r6,1
	bne 1f
	b user_segfault_handler
1:

	rldicr %r5, %r4, 0, 50  
	rldicl %r6, %r4, 0, 50  // leave lower 14 bits of RPN

	mfspr %r4, PpeTlbIndexHint
	
		/* index + LVPN */
	srdi %r2,%r15,12
	andi. %r2,%r2,0x7ff
	rldimi  %r4, %r2, 63-26, 16 //  put into bits 16..26
	mtspr PpeTlbIndex, %r4

		/* ARPN */
	rldicr %r2, %r5, 0, 47 // round down to 64K
	or %r2, %r2, %r6 /* OR in page attributes */
	mtspr PpeTlbRpn, %r2

		/* calculate AVPN */
	srdi %r2,%r15,12-7+11
	rldicl %r3,%r3,0,56
	ori %r2, %r2, 5 // set V|L  (valid, large)
	mtspr PpeTlbVpn, %r2

	b except_return

user_segfault_handler:
	lis %r9,vm_segfault_handler@ha - 0x80000000
	lwz %r9,vm_segfault_handler@l(%r9)

	li %r3,0
	cmplwi %r9,0
	beq dump_from_pagefault // no user handler -> call system handler

	/*
	calling user code can throw exceptions,
	to preserve original HV context base I increment context pointer before calling user
	and decrement it after, to create a sort of context stack
	*/

	mfsprg1 %r31
	mfsrr0 %r4
	std %r4, OFF_REG(REG_SRR0)(%r31)
	mfsrr1 %r4
	std %r4, OFF_REG(REG_SRR1)(%r31)
	mfsprg0 %r4
	std %r4, OFF_REG(31)(%r31)

	mfmsr %r5
	ori %r5, %r5, 0x2000 // FP available
	mtmsr %r5

	stfd %f0, OFF_FPREG(0)(%r31)
	stfd %f1, OFF_FPREG(1)(%r31)
	stfd %f2, OFF_FPREG(2)(%r31)
	stfd %f3, OFF_FPREG(3)(%r31)
	stfd %f4, OFF_FPREG(4)(%r31)
	stfd %f5, OFF_FPREG(5)(%r31)
	stfd %f6, OFF_FPREG(6)(%r31)
	stfd %f7, OFF_FPREG(7)(%r31)
	stfd %f8, OFF_FPREG(8)(%r31)
	stfd %f9, OFF_FPREG(9)(%r31)
	stfd %f10, OFF_FPREG(10)(%r31)
	stfd %f11, OFF_FPREG(11)(%r31)
	stfd %f12, OFF_FPREG(12)(%r31)
	stfd %f13, OFF_FPREG(13)(%r31)
	stfd %f14, OFF_FPREG(14)(%r31)
	stfd %f15, OFF_FPREG(15)(%r31)
	stfd %f16, OFF_FPREG(16)(%r31)
	stfd %f17, OFF_FPREG(17)(%r31)
	stfd %f18, OFF_FPREG(18)(%r31)
	stfd %f19, OFF_FPREG(19)(%r31)
	stfd %f20, OFF_FPREG(20)(%r31)
	stfd %f21, OFF_FPREG(21)(%r31)
	stfd %f22, OFF_FPREG(22)(%r31)
	stfd %f23, OFF_FPREG(23)(%r31)
	stfd %f24, OFF_FPREG(24)(%r31)
	stfd %f25, OFF_FPREG(25)(%r31)
	stfd %f26, OFF_FPREG(26)(%r31)
	stfd %f27, OFF_FPREG(27)(%r31)
	stfd %f28, OFF_FPREG(28)(%r31)
	stfd %f29, OFF_FPREG(29)(%r31)
	stfd %f30, OFF_FPREG(30)(%r31)
	stfd %f31, OFF_FPREG(31)(%r31)

	mfsprg1 %r8
	addi %r8,%r8,HV_CONTEXT_SIZE
	mtsprg1 %r8
	
	mfpir %r3
	mfsrr0 %r4
	mfdar %r5
	
	mfmsr %r8

	lis %r10, sfh32@h
	ori %r10, %r10, sfh32@l
	mtsrr0 %r10
	rfid

sfh32:
	mtsprg3 %r8

	mtctr %r9 
	bctrl

	mfsprg3 %r4
	mtsrr1 %r4
	
	lis %r4, sfh64@h - 0x80000000
	ori %r4, %r4, sfh64@l
	mtsrr0 %r4
	rfid

sfh64:
	mfsprg1 %r8
	subi %r8,%r8,HV_CONTEXT_SIZE
	mtsprg1 %r8

	mfsprg1 %r31

	lfd %f0, OFF_FPREG(0)(%r31)
	lfd %f1, OFF_FPREG(1)(%r31)
	lfd %f2, OFF_FPREG(2)(%r31)
	lfd %f3, OFF_FPREG(3)(%r31)
	lfd %f4, OFF_FPREG(4)(%r31)
	lfd %f5, OFF_FPREG(5)(%r31)
	lfd %f6, OFF_FPREG(6)(%r31)
	lfd %f7, OFF_FPREG(7)(%r31)
	lfd %f8, OFF_FPREG(8)(%r31)
	lfd %f9, OFF_FPREG(9)(%r31)
	lfd %f10, OFF_FPREG(10)(%r31)
	lfd %f11, OFF_FPREG(11)(%r31)
	lfd %f12, OFF_FPREG(12)(%r31)
	lfd %f13, OFF_FPREG(13)(%r31)
	lfd %f14, OFF_FPREG(14)(%r31)
	lfd %f15, OFF_FPREG(15)(%r31)
	lfd %f16, OFF_FPREG(16)(%r31)
	lfd %f17, OFF_FPREG(17)(%r31)
	lfd %f18, OFF_FPREG(18)(%r31)
	lfd %f19, OFF_FPREG(19)(%r31)
	lfd %f20, OFF_FPREG(20)(%r31)
	lfd %f21, OFF_FPREG(21)(%r31)
	lfd %f22, OFF_FPREG(22)(%r31)
	lfd %f23, OFF_FPREG(23)(%r31)
	lfd %f24, OFF_FPREG(24)(%r31)
	lfd %f25, OFF_FPREG(25)(%r31)
	lfd %f26, OFF_FPREG(26)(%r31)
	lfd %f27, OFF_FPREG(27)(%r31)
	lfd %f28, OFF_FPREG(28)(%r31)
	lfd %f29, OFF_FPREG(29)(%r31)
	lfd %f30, OFF_FPREG(30)(%r31)
	lfd %f31, OFF_FPREG(31)(%r31)

	cmplwi %r3,0
	bne 1f
	ld %r3, OFF_REG(REG_SRR0)(%r31)
1:
	mtsrr0 %r3
	ld %r3, OFF_REG(REG_SRR1)(%r31)
	mtsrr1 %r3
	ld %r3, OFF_REG(31)(%r31)
	mtsprg0 %r3
	
except_return:

	mfsprg1 %r31

	ld %r0, OFF_REG(0)(%r31)
	ld %r5, OFF_REG(5)(%r31)
	ld %r6, OFF_REG(6)(%r31)
	ld %r7, OFF_REG(7)(%r31)
	ld %r8, OFF_REG(8)(%r31)
	ld %r9, OFF_REG(9)(%r31)
	ld %r10, OFF_REG(10)(%r31)
	ld %r11, OFF_REG(11)(%r31)
	ld %r12, OFF_REG(12)(%r31)
	ld %r13, OFF_REG(13)(%r31)
	ld %r14, OFF_REG(14)(%r31)
	ld %r15, OFF_REG(15)(%r31)
	ld %r16, OFF_REG(16)(%r31)
	ld %r17, OFF_REG(17)(%r31)
	ld %r18, OFF_REG(18)(%r31)
	ld %r19, OFF_REG(19)(%r31)
	ld %r20, OFF_REG(20)(%r31)
	ld %r21, OFF_REG(21)(%r31)
	ld %r22, OFF_REG(22)(%r31)
	ld %r23, OFF_REG(23)(%r31)
	ld %r24, OFF_REG(24)(%r31)
	ld %r25, OFF_REG(25)(%r31)
	ld %r26, OFF_REG(26)(%r31)
	ld %r27, OFF_REG(27)(%r31)
	ld %r28, OFF_REG(28)(%r31)
	ld %r29, OFF_REG(29)(%r31)
	ld %r30, OFF_REG(30)(%r31)

	ld %r4, OFF_REG(REG_LR)(%r31)
	mtlr %r4

	ld %r4, OFF_REG(REG_CR)(%r31)
	mtcr %r4
	ld %r4, OFF_REG(REG_CTR)(%r31)
	mtctr %r4
	ld %r4, OFF_REG(REG_XER)(%r31)
	mtxer %r4

	ld %r4, OFF_REG(4)(%r31)
	ld %r3, OFF_REG(3)(%r31)
	ld %r2, OFF_REG(2)(%r31)
	ld %r1, OFF_REG(1)(%r31)
	mfsprg0 %r31

	rfid

ex_data_segment:

ex_instruction_segment:


ex_reset:
ex_machine_check:

//ex_interrupt:
ex_alignment:
ex_program:
ex_floating_point:

ex_decrementer:
ex_system_call:
ex_trace:
ex_performance:

ex_iabr:
ex_reserved:
ex_thermal:

dump:
	mfsprg1 %r31
//std %r1, OFF_REG(1)(%r31)
//std %r2, OFF_REG(2)(%r31)
//std %r3, OFF_REG(3)(%r31)
//std %r4, OFF_REG(4)(%r31)

	std %r0, OFF_REG(0)(%r31)
	std %r5, OFF_REG(5)(%r31)
	std %r6, OFF_REG(6)(%r31)
	std %r7, OFF_REG(7)(%r31)
	std %r8, OFF_REG(8)(%r31)
	std %r9, OFF_REG(9)(%r31)
	std %r10, OFF_REG(10)(%r31)
	std %r11, OFF_REG(11)(%r31)
	std %r12, OFF_REG(12)(%r31)
	std %r13, OFF_REG(13)(%r31)
	std %r14, OFF_REG(14)(%r31)
	std %r15, OFF_REG(15)(%r31)
	std %r16, OFF_REG(16)(%r31)
	std %r17, OFF_REG(17)(%r31)
	std %r18, OFF_REG(18)(%r31)
	std %r19, OFF_REG(19)(%r31)
	std %r20, OFF_REG(20)(%r31)
	std %r21, OFF_REG(21)(%r31)
	std %r22, OFF_REG(22)(%r31)
	std %r23, OFF_REG(23)(%r31)
	std %r24, OFF_REG(24)(%r31)
	std %r25, OFF_REG(25)(%r31)
	std %r26, OFF_REG(26)(%r31)
	std %r27, OFF_REG(27)(%r31)
	std %r28, OFF_REG(28)(%r31)
	std %r29, OFF_REG(29)(%r31)
	std %r30, OFF_REG(30)(%r31)
	mfsprg0 %r4
	std %r4, OFF_REG(31)(%r31)
	mflr %r4
	std %r4, OFF_REG(REG_LR)(%r31)

//	li %r3, debug_lock@l
//	bl lock

dump_from_pagefault:
	mfsprg1 %r31
	addi %r8,%r31,HV_CONTEXT_SIZE /* move to next context in stack in case there's nested segfaults */
	mtsprg1 %r8
	
	mfsrr0 %r4
	std %r4, OFF_REG(REG_SRR0)(%r31)
	mfsrr1 %r4
	std %r4, OFF_REG(REG_SRR1)(%r31)
	mfdar %r4
	std %r4, OFF_REG(REG_DAR)(%r31)
	mfpir %r4
	std %r4, OFF_REG(REG_PIR)(%r31)

	lis %r4,crashdump@h
	ori %r4,%r4,crashdump@l
	mtsrr0 %r4

	mr %r30,%r3
	mr %r3,%r31
	li %r4,HV_CONTEXT_SIZE
	bl memdcbf
	mr %r3,%r30

	oris %r4,%r31,0x8000

	rfid

putc:
/*	mfpir %r4
	cmplwi %r4,0
	bnelr*/

	lis %r4, 0x8000
	ori %r4, %r4, 0x200
	rldicr  %r4, %r4, 32,31
	oris  %r4, %r4, 0xea00

	mtsprg3 %r3
	cmplwi %r3,'\n'
	bne notnl
	
	li %r3,'\r'
	slwi %r3, %r3, 24
	stw %r3, 0x1014(%r4)
	1:
		lwz %r3, 0x1018(%r4)
		rlwinm. %r3, %r3, 0, 6, 6
		beq 1b

notnl:
	mfsprg3 %r3

	slwi %r3, %r3, 24
	stw %r3, 0x1014(%r4)
	1:
		lwz %r3, 0x1018(%r4)
		rlwinm. %r3, %r3, 0, 6, 6
		beq 1b
	blr

ex_interrupt:
	std %r0, OFF_REG(0)(%r31)
	std %r5, OFF_REG(5)(%r31)
	std %r6, OFF_REG(6)(%r31)
	std %r7, OFF_REG(7)(%r31)
	std %r8, OFF_REG(8)(%r31)
	std %r9, OFF_REG(9)(%r31)
	std %r10, OFF_REG(10)(%r31)
	std %r11, OFF_REG(11)(%r31)
	std %r12, OFF_REG(12)(%r31)
	std %r13, OFF_REG(13)(%r31)
	std %r14, OFF_REG(14)(%r31)
	std %r15, OFF_REG(15)(%r31)
	std %r16, OFF_REG(16)(%r31)
	std %r17, OFF_REG(17)(%r31)
	std %r18, OFF_REG(18)(%r31)
	std %r19, OFF_REG(19)(%r31)
	std %r20, OFF_REG(20)(%r31)
	std %r21, OFF_REG(21)(%r31)
	std %r22, OFF_REG(22)(%r31)
	std %r23, OFF_REG(23)(%r31)
	std %r24, OFF_REG(24)(%r31)
	std %r25, OFF_REG(25)(%r31)
	std %r26, OFF_REG(26)(%r31)
	std %r27, OFF_REG(27)(%r31)
	std %r28, OFF_REG(28)(%r31)
	std %r29, OFF_REG(29)(%r31)
	std %r30, OFF_REG(30)(%r31)
	mfsprg0 %r4
	std %r4, OFF_REG(31)(%r31)
	mflr %r4
	std %r4, OFF_REG(REG_LR)(%r31)
	mfcr %r4
	std %r4, OFF_REG(REG_CR)(%r31)
	mfctr %r4
	std %r4, OFF_REG(REG_CTR)(%r31)
	mfxer %r4
	std %r4, OFF_REG(REG_XER)(%r31)

	li %r3, 'I'
	bl putc
	li %r3, 'r'
	bl putc
	li %r3, 'q'
	bl putc
	li %r3, '\n'
	bl putc

	b except_return

page_fault:

	li %r3,0

	b dump_from_pagefault


puthex:
	mflr 0
	mr 30,3
	li 31,0
.L5:
	rldicl 9,30,4,60
	addi 31,31,1
	sldi 30,30,4
	addi 3,9,55+32
	cmpdi 7,9,9
	bgt 7,.L3
	addi 3,9,48
.L3:
	bl putc
	cmpdi 7,31,16
	bne 7,.L5
	mtlr 0
	blr
