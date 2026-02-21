#define CACHELINE_SIZE 128
#define CACHELINE_SIZE_LOG2 7

.globl memdcbt
memdcbt:
		/* round down address */
	li %r5, CACHELINE_SIZE - 1
	andc %r5, %r3, %r5
		/* increase size by alignment change */
	rldicl %r3, %r3, 0, 64-CACHELINE_SIZE_LOG2
	add %r4, %r4, %r3

1:
	dcbt 0, %r5
	addic %r5, %r5, CACHELINE_SIZE /* also clears carry */
	subic. %r4, %r4, CACHELINE_SIZE
	bge 1b
	sync
	blr

.globl memdcbst
memdcbst:
		/* round down address */
	li %r5, CACHELINE_SIZE - 1
	andc %r5, %r3, %r5
		/* increase size by alignment change */
	rldicl %r3, %r3, 0, 64-CACHELINE_SIZE_LOG2
	add %r4, %r4, %r3

1:
	dcbst 0, %r5
	addic %r5, %r5, CACHELINE_SIZE /* also clears carry */
	subic. %r4, %r4, CACHELINE_SIZE
	bge 1b
	sync
	blr

.globl memdcbf
memdcbf:
		/* round down address */
	li %r5, CACHELINE_SIZE - 1
	andc %r5, %r3, %r5
		/* increase size by alignment change */
	rldicl %r3, %r3, 0, 64-CACHELINE_SIZE_LOG2
	add %r4, %r4, %r3

1:
	dcbf 0, %r5
	addic %r5, %r5, CACHELINE_SIZE /* also clears carry */
	subic. %r4, %r4, CACHELINE_SIZE
	bge 1b
	sync
	blr

.globl memicbi
memicbi:
		/* round down address */
	li %r5, CACHELINE_SIZE - 1
	andc %r5, %r3, %r5
		/* increase size by alignment change */
	rldicl %r3, %r3, 0, 64-CACHELINE_SIZE_LOG2
	add %r4, %r4, %r3

1:
	dcbst 0, %r5
	icbi 0, %r5
	addic %r5, %r5, CACHELINE_SIZE /* also clears carry */
	subic. %r4, %r4, CACHELINE_SIZE
	bge 1b
	sync
	isync
	blr
