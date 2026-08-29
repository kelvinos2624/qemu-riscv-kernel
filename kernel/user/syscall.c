#include "arch/riscv64/csr.h"
#include "core/kernel.h"
#include "core/trap.h"
#include "user/syscall.h"

#define USER_SYSCALL_HANDLED 0

static uint64_t user_exit_code;

static void first_user_exit_return(void) __attribute__((noreturn));

static void first_user_exit_return(void)
{
    console_write("user: exited code=");
    console_write_hex64(user_exit_code);
    console_write("\n");
    console_write("milestone 15: first user task\n");

    for (;;) {
        __asm__ volatile("wfi");
    }
}

int user_syscall_handle(trap_frame_t *frame)
{
    if (frame == NULL || frame->a7 != USER_SYSCALL_EXIT) {
        return -1;
    }

    user_exit_code = frame->a0;
    frame->mepc = (uint64_t)(uintptr_t)first_user_exit_return;
    frame->mstatus |= SSTATUS_SPP | SSTATUS_SPIE;
    frame->sp = (uint64_t)(uintptr_t)frame + TRAP_FRAME_STACK_SIZE;

    return USER_SYSCALL_HANDLED;
}
