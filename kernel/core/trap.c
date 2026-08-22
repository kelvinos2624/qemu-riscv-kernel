#include "arch/riscv64/csr.h"
#include "core/kernel.h"
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

void trap_init(void)
{
    csr_write_mtvec((uint64_t)(uintptr_t)trap_entry);

    console_write("trap: mtvec=");
    console_write_hex64(csr_read_mtvec());
    console_write("\n");
}

void trap_selftest(void)
{
    trap_selftest_seen = 0;

    __asm__ volatile("ecall");

    if (trap_selftest_seen != 1) {
        PANIC("trap self-test did not return through handler");
    }

    console_write("trap: self-test passed\n");
}

void trap_handle(trap_frame_t *frame)
{
    const uint64_t is_interrupt = frame->mcause & MCAUSE_INTERRUPT;
    const uint64_t cause = frame->mcause & MCAUSE_CODE_MASK;

    if (!is_interrupt && cause == MCAUSE_ECALL_M_MODE && trap_selftest_seen == 0) {
        trap_selftest_seen = 1;
        frame->mepc += 4;
        return;
    }

    if (is_interrupt && cause == MCAUSE_MACHINE_TIMER_INTERRUPT) {
        timer_handle_interrupt();
        return;
    }

    console_write("\ntrap: unhandled ");
    console_write(is_interrupt ? "interrupt " : "exception ");
    trap_print_field("mcause", frame->mcause);
    trap_print_field("mepc", frame->mepc);
    trap_print_field("mtval", frame->mtval);
    console_write("\n");

    PANIC("unhandled trap");
}
