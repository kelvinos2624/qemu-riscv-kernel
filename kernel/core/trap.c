#include "arch/riscv64/csr.h"
#include "core/kernel.h"
#include "core/thread.h"
#include "core/trap.h"
#include "drivers/timer.h"

#include <stddef.h>

extern void trap_entry(void);

static volatile uint64_t trap_selftest_seen;

_Static_assert(offsetof(trap_frame_t, ra) == 0, "trap frame ra offset");
_Static_assert(offsetof(trap_frame_t, sp) == 8, "trap frame sp offset");
_Static_assert(offsetof(trap_frame_t, t0) == 32, "trap frame t0 offset");
_Static_assert(offsetof(trap_frame_t, a0) == 72, "trap frame a0 offset");
_Static_assert(offsetof(trap_frame_t, mepc) == 248, "trap frame mepc offset");
_Static_assert(offsetof(trap_frame_t, mstatus) == 256, "trap frame mstatus offset");
_Static_assert(offsetof(trap_frame_t, mcause) == 264, "trap frame mcause offset");
_Static_assert(offsetof(trap_frame_t, mtval) == 272, "trap frame mtval offset");
_Static_assert(sizeof(trap_frame_t) == 280, "trap frame C size");

static void trap_print_field(const char *name, uint64_t value)
{
    console_write(name);
    console_write("=");
    console_write_hex64(value);
    console_write(" ");
}

static int trap_is_page_fault(uint64_t cause)
{
    return cause == MCAUSE_INSTRUCTION_PAGE_FAULT ||
           cause == MCAUSE_LOAD_PAGE_FAULT ||
           cause == MCAUSE_STORE_PAGE_FAULT;
}

static const char *trap_page_fault_access(uint64_t cause)
{
    if (cause == MCAUSE_INSTRUCTION_PAGE_FAULT) {
        return "instruction";
    }

    if (cause == MCAUSE_LOAD_PAGE_FAULT) {
        return "load";
    }

    if (cause == MCAUSE_STORE_PAGE_FAULT) {
        return "store";
    }

    return "unknown";
}

static void trap_report_page_fault(trap_frame_t *frame, uint64_t cause)
{
    console_write("\ntrap: page fault access=");
    console_write(trap_page_fault_access(cause));
    console_write(" ");
    trap_print_field("scause", frame->mcause);
    trap_print_field("sepc", frame->mepc);
    trap_print_field("stval", frame->mtval);
    trap_print_field("sstatus", frame->mstatus);
    trap_print_field("satp", csr_read_satp());
    trap_print_field("tid", thread_current_tid());
    console_write("\n");

    PANIC("page fault");
}

void trap_init(void)
{
    csr_write_stvec((uint64_t)(uintptr_t)trap_entry);

    console_write("trap: stvec=");
    console_write_hex64(csr_read_stvec());
    console_write("\n");
}

void trap_selftest(void)
{
    trap_selftest_seen = 0;

    __asm__ volatile(".4byte 0x00100073" : : : "memory");

    if (trap_selftest_seen != 1) {
        PANIC("trap self-test did not return through handler");
    }

    console_write("trap: self-test passed\n");
}

trap_frame_t *trap_handle(trap_frame_t *frame)
{
    const uint64_t is_interrupt = frame->mcause & MCAUSE_INTERRUPT;
    const uint64_t cause = frame->mcause & MCAUSE_CODE_MASK;

    if (!is_interrupt && cause == MCAUSE_BREAKPOINT && trap_selftest_seen == 0) {
        trap_selftest_seen = 1;
        frame->mepc += 4;
        return frame;
    }

    if (!is_interrupt && cause == MCAUSE_BREAKPOINT) {
        return thread_handle_control_trap_from_trap(frame);
    }

    if (is_interrupt && cause == SCAUSE_SUPERVISOR_TIMER_INTERRUPT) {
        timer_handle_interrupt();
        return thread_maybe_preempt_from_trap(frame);
    }

    if (!is_interrupt && trap_is_page_fault(cause)) {
        trap_report_page_fault(frame, cause);
    }

    console_write("\ntrap: unhandled ");
    console_write(is_interrupt ? "interrupt " : "exception ");
    trap_print_field("mcause", frame->mcause);
    trap_print_field("mepc", frame->mepc);
    trap_print_field("mtval", frame->mtval);
    console_write("\n");

    PANIC("unhandled trap");
}
