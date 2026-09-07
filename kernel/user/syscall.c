#include "arch/riscv64/csr.h"
#include "core/kernel.h"
#include "core/thread.h"
#include "core/trace.h"
#include "core/trap.h"
#include "user/accel_syscall.h"
#include "user/task.h"
#include "user/syscall.h"

static uint64_t user_exit_code;
static user_syscall_benchmark_t user_noop_benchmark;

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

static void user_syscall_trace(
    trace_type_t type,
    trap_frame_t *frame,
    uint64_t arg0,
    uint64_t arg1
)
{
    if (thread_current_user_task_for_frame(frame) != NULL) {
        trace_emit(
            type,
            thread_current_tid(),
            THREAD_INVALID_TID,
            arg0,
            arg1
        );
    }
}

static trap_frame_t *user_syscall_exit(trap_frame_t *frame)
{
    user_task_t *task = thread_current_user_task_for_frame(frame);

    if (task != NULL) {
        user_syscall_trace(
            TRACE_USER_SYSCALL_RETURN,
            frame,
            USER_SYSCALL_EXIT,
            frame->a0
        );
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

void user_syscall_benchmark_reset(void)
{
    user_noop_benchmark.iterations = 0;
    user_noop_benchmark.cycles_total = 0;
    user_noop_benchmark.cycles_min = UINT64_MAX;
    user_noop_benchmark.cycles_max = 0;
}

user_syscall_benchmark_t user_syscall_benchmark_snapshot(void)
{
    user_syscall_benchmark_t snapshot = user_noop_benchmark;
    if (snapshot.iterations == 0) {
        snapshot.cycles_min = 0;
    }

    return snapshot;
}

static trap_frame_t *user_syscall_yield(trap_frame_t *frame)
{
    user_syscall_require_task(frame);
    user_syscall_advance(frame);
    frame->a0 = USER_SYSCALL_OK;
    user_syscall_trace(
        TRACE_USER_SYSCALL_RETURN,
        frame,
        USER_SYSCALL_YIELD,
        USER_SYSCALL_OK
    );
    console_write("user: syscall yield\n");
    return thread_yield_current_from_trap(frame);
}

static trap_frame_t *user_syscall_sleep(trap_frame_t *frame)
{
    const uint64_t ticks = frame->a0;

    user_syscall_require_task(frame);
    user_syscall_advance(frame);
    frame->a0 = USER_SYSCALL_OK;
    user_syscall_trace(
        TRACE_USER_SYSCALL_RETURN,
        frame,
        USER_SYSCALL_SLEEP,
        USER_SYSCALL_OK
    );
    console_write("user: syscall sleep ticks=");
    console_write_hex64(ticks);
    console_write("\n");
    return thread_sleep_current_from_trap(frame, ticks);
}

static trap_frame_t *user_syscall_noop(
    trap_frame_t *frame,
    uint64_t dispatch_start_cycle
)
{
    user_syscall_require_task(frame);
    user_syscall_advance(frame);
    frame->a0 = USER_SYSCALL_OK;
    user_syscall_trace(
        TRACE_USER_SYSCALL_RETURN,
        frame,
        USER_SYSCALL_NOOP,
        USER_SYSCALL_OK
    );

    const uint64_t elapsed = csr_read_cycle() - dispatch_start_cycle;
    user_noop_benchmark.iterations++;
    user_noop_benchmark.cycles_total += elapsed;
    if (elapsed < user_noop_benchmark.cycles_min) {
        user_noop_benchmark.cycles_min = elapsed;
    }
    if (elapsed > user_noop_benchmark.cycles_max) {
        user_noop_benchmark.cycles_max = elapsed;
    }

    return frame;
}

trap_frame_t *user_syscall_dispatch(trap_frame_t *frame)
{
    if (frame == NULL) {
        PANIC("null user syscall frame");
    }

    const uint64_t dispatch_start_cycle = csr_read_cycle();
    const uint64_t syscall_nr = frame->a7;
    user_syscall_trace(
        TRACE_USER_SYSCALL_ENTER,
        frame,
        syscall_nr,
        frame->mepc
    );

    if (syscall_nr == USER_SYSCALL_EXIT) {
        return user_syscall_exit(frame);
    }

    if (syscall_nr == USER_SYSCALL_YIELD) {
        return user_syscall_yield(frame);
    }

    if (syscall_nr == USER_SYSCALL_SLEEP) {
        return user_syscall_sleep(frame);
    }

    if (syscall_nr == USER_SYSCALL_ACCEL_MEMSET) {
        return user_accel_syscall_memset(frame);
    }

    if (syscall_nr == USER_SYSCALL_NOOP) {
        return user_syscall_noop(frame, dispatch_start_cycle);
    }

    user_syscall_require_task(frame);
    user_syscall_advance(frame);
    frame->a0 = (uint64_t)(int64_t)USER_SYSCALL_ERR_UNKNOWN;
    user_syscall_trace(
        TRACE_USER_SYSCALL_ERROR,
        frame,
        syscall_nr,
        (uint64_t)(int64_t)USER_SYSCALL_ERR_UNKNOWN
    );
    console_write("user: unknown syscall\n");
    return frame;
}
