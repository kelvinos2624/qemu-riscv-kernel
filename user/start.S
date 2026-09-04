.section .text.user_start, "ax"
.globl _start

.balign 4
_start:
    .option push
    .option norvc
    call user_main
    call user_exit
1:
    j 1b
    .option pop
