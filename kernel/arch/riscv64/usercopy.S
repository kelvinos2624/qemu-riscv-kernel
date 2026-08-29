.section .text.usercopy, "ax", @progbits

.equ USERCOPY_OK, 0
.equ USERCOPY_ERR_FAULT, -2

.globl riscv_usercopy_copy_bytes
.globl riscv_usercopy_copy_start
.globl riscv_usercopy_copy_end
.globl riscv_usercopy_copy_fixup

riscv_usercopy_copy_bytes:
riscv_usercopy_copy_start:
1:
    beqz a2, 2f
    lbu t0, 0(a1)
    sb t0, 0(a0)
    addi a1, a1, 1
    addi a0, a0, 1
    addi a2, a2, -1
    j 1b
riscv_usercopy_copy_end:
2:
    li a0, USERCOPY_OK
    ret

riscv_usercopy_copy_fixup:
    li a0, USERCOPY_ERR_FAULT
    ret
