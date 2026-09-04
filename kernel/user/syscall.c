#include "arch/riscv64/csr.h"
#include "core/kernel.h"
#include "core/thread.h"
#include "core/trap.h"
#include "user/task.h"
#include "user/syscall.h"

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

static void user_syscall_report_exit(uint64_t code)
{
    console_write("user: exited code=");
    console_write_hex64(code);
    console_write("\n");
}

static void user_syscall_advance(trap_frame_t *frame)
{
    frame->mepc += 4;
}

static trap_frame_t *user_syscall_exit(trap_frame_t *frame)
{
    user_task_t *task = thread_current_user_task_for_frame(frame);

    if (task != NULL) {
        user_syscall_report_exit(frame->a0);
        console_write("milestone 22: user address-space switching\n");
        return thread_exit_current_from_trap(frame);
    }

    user_exit_code = frame->a0;
    frame->mepc = (uint64_t)(uintptr_t)first_user_exit_return;
    frame->mstatus |= SSTATUS_SPP | SSTATUS_SPIE;
    frame->sp = (uint64_t)(uintptr_t)frame + TRAP_FRAME_STACK_SIZE;

    return frame;
}

static void user_syscall_require_task(trap_frame_t *frame)
{
    if (thread_current_user_task_for_frame(frame) == NULL) {
        PANIC("scheduled user task required for syscall");
    }
}

static trap_frame_t *user_syscall_yield(trap_frame_t *frame)
{
    user_syscall_require_task(frame);
    user_syscall_advance(frame);
    frame->a0 = USER_SYSCALL_OK;
    console_write("user: syscall yield\n");
    return thread_yield_current_from_trap(frame);
}

static trap_frame_t *user_syscall_sleep(trap_frame_t *frame)
{
    const uint64_t ticks = frame->a0;

    user_syscall_require_task(frame);
    user_syscall_advance(frame);
    frame->a0 = USER_SYSCALL_OK;
    console_write("user: syscall sleep ticks=");
    console_write_hex64(ticks);
    console_write("\n");
    return thread_sleep_current_from_trap(frame, ticks);
}

trap_frame_t *user_syscall_dispatch(trap_frame_t *frame)
{
    if (frame == NULL) {
        PANIC("null user syscall frame");
    }

    if (frame->a7 == USER_SYSCALL_EXIT) {
        return user_syscall_exit(frame);
    }

    if (frame->a7 == USER_SYSCALL_YIELD) {
        return user_syscall_yield(frame);
    }

    if (frame->a7 == USER_SYSCALL_SLEEP) {
        return user_syscall_sleep(frame);
    }

    console_write("\ntrap: unknown user ecall syscall=");
    console_write_hex64(frame->a7);
    console_write(" sepc=");
    console_write_hex64(frame->mepc);
    console_write("\n");
    PANIC("unknown user syscall");
}
