// startup when being loaded from xell. It the linux ABI.

#include <ppc/xenonsprs.h>

.globl _start

.align 8
_start:

b 1f

. = _start + 0x60

2:
	mfspr %r3, pir
	lwz %r4, secondary_lock@l(0)
	srw %r4, %r4, %r3
	rlwinm. %r4, %r4, 0, 31, 31
	beq 2b

	mfspr %r3, pir
	rlwinm. %r3, %r3, 0, 31, 31
//	bne after_slb

1:
		/* setup HID6 - done by newer Xells, added here to stay compatible with old Xells */
	lis	%r3, 1
	ori	%r3,%r3, 0x8038
	rldicr	%r3, %r3, 32,31
	mtspr	hid6, %r3
	sync
	isync

		/* setup SLB identity mapping, N=0, LP=1 */
	isync
	slbia
	isync

		/* set page size: 64K for both */
	mfspr %r3, hid6
	li %r4, 0xF
	sldi %r4, %r4, 44
	andc %r3, %r3, %r4
	li %r4, 0xa
	sldi %r4, %r4, 44
	or %r3, %r3, %r4
	mtspr  hid6, %r3

	li %r3, 0x10
	mtctr %r3
	li %r4,  0x100 // rs
	lis %r5, 0x0800 // rb (valid)
	lis %r6, 0x1000 // ESID increase
	ori %r6, %r6, 1 // index
	li %r7, 0x1000
1:
	slbmte %r4, %r5

	add %r5, %r5, %r6
	add %r4, %r4, %r7

	bdnz+ 1b

after_slb:

		/* invalidate all TLBs */
	li      %r3, 0xC00
	li      %r4, 0x100
	mtctr   %r4
1:
	tlbiel  %r3, 0
	addi    %r3, %r3, 0x1000
	bdnz+ 1b
	eieio
	ptesync
	li      %r3, 0x3FF
	rldicr  %r3, %r3, 32,31
	tlbiel  %r3, 1
	eieio
	ptesync

		/* setup MSR */
	mfmsr %r5
	ori %r5, %r5, 0x1000 // Machine check enable
	ori %r5, %r5, 0x2000 // FP available
	oris %r5, %r5, 0x200 // Vector avail
	// ori %r5, %r5, 0x8000 // External Interrupt enable
	ori %r5, %r5, 0x10 // data relocate
	ori %r5, %r5, 0x20 // instruction relocate
	rldicl %r5, %r5, 0, 1 // clear SF (set 32bit mode)
	mtsrr1 %r5

	li %r3, 0
	mtspr lpidr, %r3

	//mfspr %r5, lpcr
	//ori %r5, %r5, 0x402 // software tlb reload + force RMCI
        //li %r3, 0xC
        li %r5, 0x402
	mtspr lpcr, %r5

	li %r3, 1  // hrmor
	sldi %r3, %r3, 63
	mtspr hrmor, %r3

	lis %r1, 0
	rldicr  %r1, %r1, 32,31
	oris %r1, %r1, __libc_stack_end@h
	ori %r1, %r1, __libc_stack_end@l

	mfspr %r3, pir
	addi %r3, %r3, 1
	slwi %r3, %r3, 16 // 0x10000 bytes per thread
	lis %r4,0x8000
	andc %r4,%r1,%r4
	sub %r3, %r4, %r3

	mtsprg1 %r3 // HV context base

        lis %r13, 0
        oris %r13, %r13, processor_blocks@h
        ori %r13, %r13, processor_blocks@l
        mfspr %r3, pir
        slwi %r3, %r3, 12 // 0x1000 bytes of processor space per thread
        add %r13, %r13, %r3 // Processor data
        
        mr %r3, %r13
        lis %r4, 0x8000
        andc %r3, %r3, %r4
        mtspr hsprg0, %r3 // Physical address of the processor data

        mfspr %r4, pir
        stb %r4, 0x140(%r3) // What processor are we?
        
        li %r4, 2
        stb %r4, 0x141(%r3) // Irq

	lis %r3,8
	subf %r1, %r3, %r1

		/* wake up secondary thread */
	li %r4, 0
	mfspr %r3, pir
	ori %r3, %r3, 1
	li %r5, 1
	slw %r5, %r5, %r3
	li %r3, secondary_lock@l
	bl atomic_clearset

	li %r4, 0
	mfspr %r3, pir
	li %r5, 1
	slw %r5, %r5, %r3
	li %r3, secondary_alive@l
	bl atomic_clearset

		/* now jump to primary or secondary entry point */
	mfspr %r3, pir
	cmplwi %r3, 0

	lis %r3, b32@h
	ori %r3, %r3, b32@l

	beq 1f
	lis %r3, secondary@h
	ori %r3, %r3, secondary@l
1:
	mtsrr0 %r3

	rfid

b32:
	bl c_register_frame
	bl call_ctors
	bl main

	bl exit
	
	b .

.globl wait
wait:
	.long 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

secondary:
1:
	or %r1, %r1, %r1
	lis %r3, wait@h
	ori %r3, %r3, wait@l
	mfspr %r5, pir
	slwi %r5, %r5, 3
	add %r3, %r3, %r5

	lwz %r4, 0(%r3)
	cmpwi %r4, 0
	beq 1b

	lwz %r1, 4(%r3)

	or %r2, %r2, %r2
	mtctr %r4

	bctrl

	lis %r3, wait@h
	ori %r3, %r3, wait@l
	mfspr %r5, pir
	slwi %r5, %r5, 3
	add %r3, %r3, %r5
	li %r4, 0
	stw %r4, 0(%r3)
	stw %r4, 4(%r3)

	b 1b

.globl return_to_xell
return_to_xell:
        	/* Init regs */
	mr	%r31,%r3
	mr	%r30,%r4

	mr	%r5,%r31
	mr	%r6,%r30
	oris	%r6,%r6,0x8000

		/* Copy from Flash, src = %r5, dst = %r6 */
	lis	%r4,1 /* 256k */
	mtctr   %r4

	1:	lwz     %r8,0(%r5)		//Memcopy
		stw     %r8,0(%r6)
		dcbst   %r0,%r6		//Flush cache to ram
		icbi	%r0,%r6
		sync	0
		isync
		addi    %r6,%r6,4
		addi    %r5,%r5,4
		bdnz    1b

		/* Clear IR/DR/EE, set 64 bit mode and jump to Xell*/
	li	%r4,0x30
        ori     %r4,%r4,0x8000
	mfmsr	%r3
	andc	%r3,%r3,%r4
	lis 	%r4,0x8000
	rldicr	%r4,%r4,32,31
	or	%r3,%r3,%r4
	mtsrr1	%r3

	lis	%r3,0x8000
	rldicr	%r3,%r3,32,31
	or 	%r3,%r3,%r30
	ori	%r3,%r3,0x4 /* 'start_from_libxenon' entry point */
	mtsrr0	%r3

	rfid
