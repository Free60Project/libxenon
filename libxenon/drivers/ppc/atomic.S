
	// Taken from: http://www.ibm.com/developerworks/library/pa-atom/ 
.globl atomic_inc
atomic_inc:
1:
	lwarx  %r4, 0, %r3
	addi   %r4, %r4, 1
	stwcx. %r4, 0, %r3
	bne-   1b
	blr

.globl atomic_dec
atomic_dec:
1:
	lwarx  %r4, 0, %r3
	subi   %r4, %r4, 1
	stwcx. %r4, 0, %r3
	bne-   1b
	blr

.globl atomic_clearset
atomic_clearset:
1:
	lwarx  %r6, 0, %r3
	andc   %r6, %r6, %r4
	or     %r6, %r6, %r5
	stwcx. %r6, 0, %r3
	bne-   1b
	blr
.globl atomic_compareswap
atomic_compareswap:
        li     %r7, 0
        mfmsr  %r8
1:
        mtmsrd %r7, 1 // Turn off interrupts
        lwarx  %r6, 0, %r3
        mr     %r9, %r6 // Save value in r9
        cmplwi cr7, %r6, %r5
        bne    cr7, 2f
        mr     %r6, %r4
2:
        stwcx. %r6, 0, %r3
        mtmsrd %r8, 1 // Restore them to what they were before
        bne    1b // If unable to store, retry
        mr     %r3, %r9
        lwsync
        blr

	// thanks cOz for the following :)
.globl lock
lock:
        li      %r8, 0
1:
        mfmsr   %r7
        mtmsrd  %r8, 1 // Disable interrupts
        lwarx   %r4, 0, %r3
        cmplwi  cr7, %r4, 0
        bne     cr7, 2f // If r4 is not zero, dont modify
        mr      %r4, %r3
2:
        stwcx.  %r4, 0, %r3
        mtmsrd  %r7, 1 // Restore interrupts
        bne     cr7, 3f // r4 was not zero, lock again
        bne     3f // Reservation invalid, lock again
        lwsync
        blr
3:
        db16cyc
        b       1b
.globl unlock
unlock:
        li      %r4, 0
        lwsync
        stw     %r4, 0(%r3)
        blr