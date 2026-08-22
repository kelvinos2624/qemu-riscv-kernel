.section .text
.globl context_switch

.equ SWITCH_RA,   0
.equ SWITCH_S0,   8
.equ SWITCH_S1,   16
.equ SWITCH_S2,   24
.equ SWITCH_S3,   32
.equ SWITCH_S4,   40
.equ SWITCH_S5,   48
.equ SWITCH_S6,   56
.equ SWITCH_S7,   64
.equ SWITCH_S8,   72
.equ SWITCH_S9,   80
.equ SWITCH_S10,  88
.equ SWITCH_S11,  96
.equ SWITCH_SIZE, 112

/*
 * void context_switch(uintptr_t *old_sp, uintptr_t new_sp)
 *
 * The caller must have interrupts disabled. The current thread's callee-saved
 * context is pushed onto its stack, the resulting SP is stored through old_sp,
 * then the next thread's saved stack is restored and returned into.
 */
.balign 4
context_switch:
    addi sp, sp, -SWITCH_SIZE
    sd ra, SWITCH_RA(sp)
    sd s0, SWITCH_S0(sp)
    sd s1, SWITCH_S1(sp)
    sd s2, SWITCH_S2(sp)
    sd s3, SWITCH_S3(sp)
    sd s4, SWITCH_S4(sp)
    sd s5, SWITCH_S5(sp)
    sd s6, SWITCH_S6(sp)
    sd s7, SWITCH_S7(sp)
    sd s8, SWITCH_S8(sp)
    sd s9, SWITCH_S9(sp)
    sd s10, SWITCH_S10(sp)
    sd s11, SWITCH_S11(sp)

    sd sp, 0(a0)
    mv sp, a1

    ld ra, SWITCH_RA(sp)
    ld s0, SWITCH_S0(sp)
    ld s1, SWITCH_S1(sp)
    ld s2, SWITCH_S2(sp)
    ld s3, SWITCH_S3(sp)
    ld s4, SWITCH_S4(sp)
    ld s5, SWITCH_S5(sp)
    ld s6, SWITCH_S6(sp)
    ld s7, SWITCH_S7(sp)
    ld s8, SWITCH_S8(sp)
    ld s9, SWITCH_S9(sp)
    ld s10, SWITCH_S10(sp)
    ld s11, SWITCH_S11(sp)
    addi sp, sp, SWITCH_SIZE
    ret
