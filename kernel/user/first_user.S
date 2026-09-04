.section .rodata.user_first, "a"
.globl first_user_start
.globl first_user_end
.globl syscall_basic_user_start
.globl syscall_basic_user_end

.balign 4
first_user_start:
    .option push
    .option norvc
    li a0, 0
    li a7, 1
    ecall
1:
    j 1b
    .option pop
first_user_end:

.balign 4
syscall_basic_user_start:
    .option push
    .option norvc
    li a7, 2
    ecall
    bnez a0, 1f
    li a0, 2
    li a7, 3
    ecall
    bnez a0, 1f
    li a0, 0
    li a7, 1
    ecall
1:
    li a0, 1
    li a7, 1
    ecall
2:
    j 2b
    .option pop
syscall_basic_user_end:
