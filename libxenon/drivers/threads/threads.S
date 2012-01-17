// Threading utility

#include <ppc/xenonsprs.h>

#define OFF_REG(r) r * 8
#define REG_LR 32
#define REG_CTR 33
#define REG_CR 34
#define REG_XER 35
#define REG_SRR0 36
#define REG_SRR1 37

// Threading stuff

.globl thread_disable_interrupts
thread_disable_interrupts:
    li %r4, 0
    mfmsr %r3
    mtmsrd %r4, 1
    blr
.globl thread_enable_interrupts
thread_enable_interrupts:
    mtmsrd %r3, 1
    blr

.globl spinlock
spinlock:
        li      %r8, 0
1:
        mfmsr   %r7
        mtmsrd  %r8, 1 // Disable interrupts
        lwarx   %r4, 0, %r3
        cmplwi  cr7, %r4, 0
        bne     cr7, 2f // If r4 is not zero, dont modify
        mr      %r4, %r3
2:
        stwcx.  %r13, 0, %r3
        mtmsrd  %r7, 1 // Restore interrupts
        bne     cr7, 3f // r4 was not zero, lock again
        bne     3f // Reservation invalid, lock again
        lwsync
        blr
3:
        db16cyc
        b       1b

.globl common_interrupt_handler
common_interrupt_handler:
    // Save registers 0-2, 4-12, 14-31
    .irp reg, 0,1,2,4,5,6,7,8,9,10,11,12,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    std %r\reg, OFF_REG(\reg)(%r13)
    .endr

// Dont do this for now    
/*
    // Setup the stack
    lis %r1, processor_interrupt_stack@h
    ori %r1, %r1, processor_interrupt_stack@l
    lbz %r4, 0x140(%r13) // Processor ID
    slwi %r4, %r4, 12
    add %r1, %r4, %r1
    addi %r1, %r1, 4096-8 // Put at top of stack
*/

    // Launch the handler that the hv kindly put in our link register
    // While also putting the return function in the link register
    mflr %r4
    mtctr %r4

    lis %r4, common_interrupt_return@h
    ori %r4, %r4, common_interrupt_return@l
    mtlr %r4
    bctr

.globl common_interrupt_return
common_interrupt_return:
    // Restore registers
    ld %r3, OFF_REG(REG_LR)(%r13)
    mtlr %r3
    ld %r3, OFF_REG(REG_CTR)(%r13)
    mtctr %r3
    ld %r3, OFF_REG(REG_CR)(%r13)
    mtcr %r3
    ld %r3, OFF_REG(REG_XER)(%r13)
    mtxer %r3
    ld %r3, OFF_REG(REG_SRR0)(%r13)
    mtsrr0 %r3
    ld %r3, OFF_REG(REG_SRR1)(%r13)
    mtsrr1 %r3

    .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    ld %r\reg, OFF_REG(\reg)(%r13)
    .endr

    ld %r13, OFF_REG(13)(%r13)

    // Leave the interrupt
    rfid

// Takes a PROCESSOR_FPU_VPU_SAVE pointer on r3
.globl save_floating_point
save_floating_point:
    // Save floating point (mffs) (stfdu)
    .irp reg, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    stfd %r\reg, OFF_REG(\reg)(%r3)
    .endr

    mffs %r4 // Status
    stfd %r31, OFF_REG(31)(%r3)
    blr

// Takes a PROCESSOR_FPU_VPU_SAVE pointer on r3
.globl restore_floating_point
restore_floating_point:
    .irp reg, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
    lfd %r\reg, OFF_REG(\reg)(%r3)
    .endr
    lfd %r31, OFF_REG(32)(%r3)
    mtfsf 0xFF, %r31  // Status
    lfd %r31, OFF_REG(31)(%r3)
    blr

// GCC doesnt like vectors, so im commenting this out
/*
.globl save_vector
save_vector: // 0x108
    li %r7, 0x108
    addi %r4, %r3, 0x10
    addi %r5, %r3, 0x20
    addi %r6, %r3, 0x30
    mfvscr %v0 // put vscr into v0

    stvxl %v0, %r3, %r7
    stvxl %v1, %r4, %r7
    stvxl %v2, %r5, %r7
    stvxl %v3, %r6, %r7
    li %r7, 0x148
    stvxl %v4, %r3, %r7
    stvxl %v5, %r4, %r7
    stvxl %v6, %r5, %r7
    stvxl %v7, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v8, %r3, %r7
    stvxl %v9, %r4, %r7
    stvxl %v10, %r5, %r7
    stvxl %v11, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v12, %r3, %r7
    stvxl %v13, %r4, %r7
    stvxl %v14, %r5, %r7
    stvxl %v15, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v16, %r3, %r7
    stvxl %v17, %r4, %r7
    stvxl %v18, %r5, %r7
    stvxl %v19, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v20, %r3, %r7
    stvxl %v21, %r4, %r7
    stvxl %v22, %r5, %r7
    stvxl %v23, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v24, %r3, %r7
    stvxl %v25, %r4, %r7
    stvxl %v26, %r5, %r7
    stvxl %v27, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v28, %r3, %r7
    stvxl %v29, %r4, %r7
    stvxl %v30, %r5, %r7
    stvxl %v31, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v32, %r3, %r7
    stvxl %v33, %r4, %r7
    stvxl %v34, %r5, %r7
    stvxl %v35, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v36, %r3, %r7
    stvxl %v37, %r4, %r7
    stvxl %v38, %r5, %r7
    stvxl %v39, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v40, %r3, %r7
    stvxl %v41, %r4, %r7
    stvxl %v42, %r5, %r7
    stvxl %v43, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v44, %r3, %r7
    stvxl %v45, %r4, %r7
    stvxl %v46, %r5, %r7
    stvxl %v47, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v48, %r3, %r7
    stvxl %v49, %r4, %r7
    stvxl %v50, %r5, %r7
    stvxl %v51, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v52, %r3, %r7
    stvxl %v53, %r4, %r7
    stvxl %v54, %r5, %r7
    stvxl %v55, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v56, %r3, %r7
    stvxl %v57, %r4, %r7
    stvxl %v58, %r5, %r7
    stvxl %v59, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v60, %r3, %r7
    stvxl %v61, %r4, %r7
    stvxl %v62, %r5, %r7
    stvxl %v63, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v64, %r3, %r7
    stvxl %v65, %r4, %r7
    stvxl %v66, %r5, %r7
    stvxl %v67, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v68, %r3, %r7
    stvxl %v69, %r4, %r7
    stvxl %v70, %r5, %r7
    stvxl %v71, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v72, %r3, %r7
    stvxl %v73, %r4, %r7
    stvxl %v74, %r5, %r7
    stvxl %v75, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v76, %r3, %r7
    stvxl %v77, %r4, %r7
    stvxl %v78, %r5, %r7
    stvxl %v79, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v80, %r3, %r7
    stvxl %v81, %r4, %r7
    stvxl %v82, %r5, %r7
    stvxl %v83, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v84, %r3, %r7
    stvxl %v85, %r4, %r7
    stvxl %v86, %r5, %r7
    stvxl %v87, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v88, %r3, %r7
    stvxl %v89, %r4, %r7
    stvxl %v90, %r5, %r7
    stvxl %v91, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v92, %r3, %r7
    stvxl %v93, %r4, %r7
    stvxl %v94, %r5, %r7
    stvxl %v95, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v96, %r3, %r7
    stvxl %v97, %r4, %r7
    stvxl %v98, %r5, %r7
    stvxl %v99, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v100, %r3, %r7
    stvxl %v101, %r4, %r7
    stvxl %v102, %r5, %r7
    stvxl %v103, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v100, %r3, %r7
    stvxl %v101, %r4, %r7
    stvxl %v102, %r5, %r7
    stvxl %v103, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v104, %r3, %r7
    stvxl %v105, %r4, %r7
    stvxl %v106, %r5, %r7
    stvxl %v107, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v108, %r3, %r7
    stvxl %v109, %r4, %r7
    stvxl %v110, %r5, %r7
    stvxl %v111, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v112, %r3, %r7
    stvxl %v113, %r4, %r7
    stvxl %v114, %r5, %r7
    stvxl %v115, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v116, %r3, %r7
    stvxl %v117, %r4, %r7
    stvxl %v118, %r5, %r7
    stvxl %v119, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v120, %r3, %r7
    stvxl %v121, %r4, %r7
    stvxl %v122, %r5, %r7
    stvxl %v123, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v124, %r3, %r7
    stvxl %v125, %r4, %r7
    stvxl %v126, %r5, %r7
    stvxl %v127, %r6, %r7
    addi %r7, %r7, 0x40
    stvxl %v0, %r3, %r7 // store vscr
    blr

.globl restore_vector
restore_vector:

*/