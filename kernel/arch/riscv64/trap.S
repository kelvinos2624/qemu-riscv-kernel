.section .text
.globl trap_entry
.globl trap_restore
.globl machine_trap_entry
.globl user_trap_entry
.globl user_trampoline_userret

.equ TF_RA,      0
.equ TF_SP,      8
.equ TF_GP,      16
.equ TF_TP,      24
.equ TF_T0,      32
.equ TF_T1,      40
.equ TF_T2,      48
.equ TF_S0,      56
.equ TF_S1,      64
.equ TF_A0,      72
.equ TF_A1,      80
.equ TF_A2,      88
.equ TF_A3,      96
.equ TF_A4,      104
.equ TF_A5,      112
.equ TF_A6,      120
.equ TF_A7,      128
.equ TF_S2,      136
.equ TF_S3,      144
.equ TF_S4,      152
.equ TF_S5,      160
.equ TF_S6,      168
.equ TF_S7,      176
.equ TF_S8,      184
.equ TF_S9,      192
.equ TF_S10,     200
.equ TF_S11,     208
.equ TF_T3,      216
.equ TF_T4,      224
.equ TF_T5,      232
.equ TF_T6,      240
.equ TF_MEPC,    248
.equ TF_MSTATUS, 256
.equ TF_MCAUSE,  264
.equ TF_MTVAL,   272
.equ TF_SIZE,    288
.equ UTC_KERNEL_SATP,         280
.equ UTC_USER_SATP,           288
.equ UTC_KERNEL_TRAP_HANDLER, 296
.equ UTC_KERNEL_TRAP_CONTEXT, 304
.equ UTC_KERNEL_STVEC,        312
.equ UTC_KERNEL_SP,           320

.section .text.trampoline,"ax"
user_trap_entry:
    csrrw sp, sscratch, sp

    sd ra, TF_RA(sp)
    sd t0, TF_T0(sp)
    csrr t0, sscratch
    sd t0, TF_SP(sp)
    sd gp, TF_GP(sp)
    sd tp, TF_TP(sp)
    sd t1, TF_T1(sp)
    sd t2, TF_T2(sp)
    sd s0, TF_S0(sp)
    sd s1, TF_S1(sp)
    sd a0, TF_A0(sp)
    sd a1, TF_A1(sp)
    sd a2, TF_A2(sp)
    sd a3, TF_A3(sp)
    sd a4, TF_A4(sp)
    sd a5, TF_A5(sp)
    sd a6, TF_A6(sp)
    sd a7, TF_A7(sp)
    sd s2, TF_S2(sp)
    sd s3, TF_S3(sp)
    sd s4, TF_S4(sp)
    sd s5, TF_S5(sp)
    sd s6, TF_S6(sp)
    sd s7, TF_S7(sp)
    sd s8, TF_S8(sp)
    sd s9, TF_S9(sp)
    sd s10, TF_S10(sp)
    sd s11, TF_S11(sp)
    sd t3, TF_T3(sp)
    sd t4, TF_T4(sp)
    sd t5, TF_T5(sp)
    sd t6, TF_T6(sp)

    csrr t0, sepc
    sd t0, TF_MEPC(sp)
    csrr t0, sstatus
    sd t0, TF_MSTATUS(sp)
    csrr t0, scause
    sd t0, TF_MCAUSE(sp)
    csrr t0, stval
    sd t0, TF_MTVAL(sp)

    ld t0, UTC_KERNEL_SATP(sp)
    ld t1, UTC_KERNEL_TRAP_HANDLER(sp)
    ld t2, UTC_KERNEL_TRAP_CONTEXT(sp)
    ld t3, UTC_KERNEL_STVEC(sp)
    ld t4, UTC_KERNEL_SP(sp)
    csrw satp, t0
    sfence.vma
    csrw stvec, t3
    csrw sscratch, zero
    mv a0, t2
    mv sp, t4
    jr t1

user_trampoline_userret:
    la t0, user_trap_entry
    csrw stvec, t0
    csrw satp, a0
    sfence.vma
    mv sp, a1

    ld t0, TF_MEPC(sp)
    csrw sepc, t0
    ld t0, TF_MSTATUS(sp)
    csrw sstatus, t0
    csrw sscratch, sp

    ld ra, TF_RA(sp)
    ld gp, TF_GP(sp)
    ld tp, TF_TP(sp)
    ld t0, TF_T0(sp)
    ld t1, TF_T1(sp)
    ld t2, TF_T2(sp)
    ld s0, TF_S0(sp)
    ld s1, TF_S1(sp)
    ld a0, TF_A0(sp)
    ld a1, TF_A1(sp)
    ld a2, TF_A2(sp)
    ld a3, TF_A3(sp)
    ld a4, TF_A4(sp)
    ld a5, TF_A5(sp)
    ld a6, TF_A6(sp)
    ld a7, TF_A7(sp)
    ld s2, TF_S2(sp)
    ld s3, TF_S3(sp)
    ld s4, TF_S4(sp)
    ld s5, TF_S5(sp)
    ld s6, TF_S6(sp)
    ld s7, TF_S7(sp)
    ld s8, TF_S8(sp)
    ld s9, TF_S9(sp)
    ld s10, TF_S10(sp)
    ld s11, TF_S11(sp)
    ld t3, TF_T3(sp)
    ld t4, TF_T4(sp)
    ld t5, TF_T5(sp)
    ld t6, TF_T6(sp)
    ld sp, TF_SP(sp)
    sret

.section .text

.balign 4
trap_entry:
    csrrw sp, sscratch, sp
    bnez sp, trap_entry_from_user

    csrrw sp, sscratch, sp
    addi sp, sp, -TF_SIZE

    sd ra, TF_RA(sp)
    sd t0, TF_T0(sp)
    addi t0, sp, TF_SIZE
    sd t0, TF_SP(sp)
    sd gp, TF_GP(sp)
    sd tp, TF_TP(sp)
    sd t1, TF_T1(sp)
    sd t2, TF_T2(sp)
    sd s0, TF_S0(sp)
    sd s1, TF_S1(sp)
    sd a0, TF_A0(sp)
    sd a1, TF_A1(sp)
    sd a2, TF_A2(sp)
    sd a3, TF_A3(sp)
    sd a4, TF_A4(sp)
    sd a5, TF_A5(sp)
    sd a6, TF_A6(sp)
    sd a7, TF_A7(sp)
    sd s2, TF_S2(sp)
    sd s3, TF_S3(sp)
    sd s4, TF_S4(sp)
    sd s5, TF_S5(sp)
    sd s6, TF_S6(sp)
    sd s7, TF_S7(sp)
    sd s8, TF_S8(sp)
    sd s9, TF_S9(sp)
    sd s10, TF_S10(sp)
    sd s11, TF_S11(sp)
    sd t3, TF_T3(sp)
    sd t4, TF_T4(sp)
    sd t5, TF_T5(sp)
    sd t6, TF_T6(sp)
    j trap_entry_save_csrs

trap_entry_from_user:
    addi sp, sp, -TF_SIZE

    sd ra, TF_RA(sp)
    sd t0, TF_T0(sp)
    csrr t0, sscratch
    sd t0, TF_SP(sp)
    csrw sscratch, zero
    sd gp, TF_GP(sp)
    sd tp, TF_TP(sp)
    sd t1, TF_T1(sp)
    sd t2, TF_T2(sp)
    sd s0, TF_S0(sp)
    sd s1, TF_S1(sp)
    sd a0, TF_A0(sp)
    sd a1, TF_A1(sp)
    sd a2, TF_A2(sp)
    sd a3, TF_A3(sp)
    sd a4, TF_A4(sp)
    sd a5, TF_A5(sp)
    sd a6, TF_A6(sp)
    sd a7, TF_A7(sp)
    sd s2, TF_S2(sp)
    sd s3, TF_S3(sp)
    sd s4, TF_S4(sp)
    sd s5, TF_S5(sp)
    sd s6, TF_S6(sp)
    sd s7, TF_S7(sp)
    sd s8, TF_S8(sp)
    sd s9, TF_S9(sp)
    sd s10, TF_S10(sp)
    sd s11, TF_S11(sp)
    sd t3, TF_T3(sp)
    sd t4, TF_T4(sp)
    sd t5, TF_T5(sp)
    sd t6, TF_T6(sp)

trap_entry_save_csrs:
    csrr t0, sepc
    sd t0, TF_MEPC(sp)
    csrr t0, sstatus
    sd t0, TF_MSTATUS(sp)
    csrr t0, scause
    sd t0, TF_MCAUSE(sp)
    csrr t0, stval
    sd t0, TF_MTVAL(sp)

    mv a0, sp
    call trap_handle
    call trap_return_from_handler

trap_restore_frame:
    ld t0, TF_MEPC(sp)
    csrw sepc, t0
    ld t0, TF_MSTATUS(sp)
    andi t1, t0, 0x100
    bnez t1, trap_restore_supervisor
    addi t1, sp, TF_SIZE
    csrw sscratch, t1
    j trap_restore_status

trap_restore_supervisor:
    csrw sscratch, zero

trap_restore_status:
    csrw sstatus, t0

    ld ra, TF_RA(sp)
    ld gp, TF_GP(sp)
    ld tp, TF_TP(sp)
    ld t0, TF_T0(sp)
    ld t1, TF_T1(sp)
    ld t2, TF_T2(sp)
    ld s0, TF_S0(sp)
    ld s1, TF_S1(sp)
    ld a0, TF_A0(sp)
    ld a1, TF_A1(sp)
    ld a2, TF_A2(sp)
    ld a3, TF_A3(sp)
    ld a4, TF_A4(sp)
    ld a5, TF_A5(sp)
    ld a6, TF_A6(sp)
    ld a7, TF_A7(sp)
    ld s2, TF_S2(sp)
    ld s3, TF_S3(sp)
    ld s4, TF_S4(sp)
    ld s5, TF_S5(sp)
    ld s6, TF_S6(sp)
    ld s7, TF_S7(sp)
    ld s8, TF_S8(sp)
    ld s9, TF_S9(sp)
    ld s10, TF_S10(sp)
    ld s11, TF_S11(sp)
    ld t3, TF_T3(sp)
    ld t4, TF_T4(sp)
    ld t5, TF_T5(sp)
    ld t6, TF_T6(sp)
    ld sp, TF_SP(sp)
    sret

trap_restore:
    mv sp, a0
    j trap_restore_frame

.balign 4
machine_trap_entry:
    addi sp, sp, -TF_SIZE

    sd ra, TF_RA(sp)
    sd t0, TF_T0(sp)
    addi t0, sp, TF_SIZE
    sd t0, TF_SP(sp)
    sd gp, TF_GP(sp)
    sd tp, TF_TP(sp)
    sd t1, TF_T1(sp)
    sd t2, TF_T2(sp)
    sd s0, TF_S0(sp)
    sd s1, TF_S1(sp)
    sd a0, TF_A0(sp)
    sd a1, TF_A1(sp)
    sd a2, TF_A2(sp)
    sd a3, TF_A3(sp)
    sd a4, TF_A4(sp)
    sd a5, TF_A5(sp)
    sd a6, TF_A6(sp)
    sd a7, TF_A7(sp)
    sd s2, TF_S2(sp)
    sd s3, TF_S3(sp)
    sd s4, TF_S4(sp)
    sd s5, TF_S5(sp)
    sd s6, TF_S6(sp)
    sd s7, TF_S7(sp)
    sd s8, TF_S8(sp)
    sd s9, TF_S9(sp)
    sd s10, TF_S10(sp)
    sd s11, TF_S11(sp)
    sd t3, TF_T3(sp)
    sd t4, TF_T4(sp)
    sd t5, TF_T5(sp)
    sd t6, TF_T6(sp)

    csrr t0, mepc
    sd t0, TF_MEPC(sp)
    csrr t0, mstatus
    sd t0, TF_MSTATUS(sp)
    csrr t0, mcause
    sd t0, TF_MCAUSE(sp)
    csrr t0, mtval
    sd t0, TF_MTVAL(sp)

    mv a0, sp
    call machine_trap_handle
    mv sp, a0

machine_trap_restore_frame:
    ld t0, TF_MEPC(sp)
    csrw mepc, t0
    ld t0, TF_MSTATUS(sp)
    csrw mstatus, t0

    ld ra, TF_RA(sp)
    ld gp, TF_GP(sp)
    ld tp, TF_TP(sp)
    ld t0, TF_T0(sp)
    ld t1, TF_T1(sp)
    ld t2, TF_T2(sp)
    ld s0, TF_S0(sp)
    ld s1, TF_S1(sp)
    ld a0, TF_A0(sp)
    ld a1, TF_A1(sp)
    ld a2, TF_A2(sp)
    ld a3, TF_A3(sp)
    ld a4, TF_A4(sp)
    ld a5, TF_A5(sp)
    ld a6, TF_A6(sp)
    ld a7, TF_A7(sp)
    ld s2, TF_S2(sp)
    ld s3, TF_S3(sp)
    ld s4, TF_S4(sp)
    ld s5, TF_S5(sp)
    ld s6, TF_S6(sp)
    ld s7, TF_S7(sp)
    ld s8, TF_S8(sp)
    ld s9, TF_S9(sp)
    ld s10, TF_S10(sp)
    ld s11, TF_S11(sp)
    ld t3, TF_T3(sp)
    ld t4, TF_T4(sp)
    ld t5, TF_T5(sp)
    ld t6, TF_T6(sp)
    ld sp, TF_SP(sp)
    mret
