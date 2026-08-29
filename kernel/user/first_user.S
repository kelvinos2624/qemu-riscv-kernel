.section .rodata.user_first, "a"
.globl first_user_start
.globl first_user_end

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
