#include "xenonsprs.h"

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

// thanks cOz for the following :)
.globl lock
lock:
        li      %r8, 0
1:
        mfmsr   %r7
		mtmsrd  %r8, 1
        lwarx   %r4, 0, %r3
        cmplwi  cr7, %r4, 0
        bne     cr7, 2f
		mr      %r4, %r3
2:
        stwcx.  %r4, 0, %r3
        mtmsrd  %r7, 1
        bne     cr7, 2f
        bne     2f
        lwsync
        blr
2:
        db16cyc
        b 1b
 
.globl unlock
unlock:
        li      %r4, 0
        sync    1  // lwsync
        stw     %r4, 0(%r3)
        li      %r4, -1
        mtspr   hdec, %r4 // reset the hypervisor decrementer to fix a lock issue
        blr