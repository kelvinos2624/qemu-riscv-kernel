#include "arch/riscv64/csr.h"
#include "arch/riscv64/machine.h"
#include "core/kernel.h"
#include "core/thread.h"
#include "drivers/mmio.h"
#include "drivers/timer.h"

static volatile uint64_t ticks;
static uint64_t next_compare;

static uint64_t timer_read_mtime(void)
{
    return mmio_read64(TIMER_CLINT_MTIME);
}

static void timer_set_deadline(uint64_t value)
{
    if (machine_call_set_timer(value) != 0) {
        PANIC("machine timer call failed");
    }
}

void timer_init(void)
{
    ticks = 0;
    next_compare = timer_read_mtime() + TIMER_INTERVAL_CYCLES;
    timer_set_deadline(next_compare);

    csr_set_sie(SIE_STIE);
    csr_set_sstatus(SSTATUS_SIE);

    console_write("timer: interval=");
    console_write_hex64(TIMER_INTERVAL_CYCLES);
    console_write(" sie=");
    console_write_hex64(csr_read_sie());
    console_write("\n");
}

void timer_handle_interrupt(void)
{
    const uint64_t now = timer_read_mtime();

    ticks++;

    do {
        next_compare += TIMER_INTERVAL_CYCLES;
    } while (next_compare <= now);

    timer_set_deadline(next_compare);
    thread_on_timer_tick();
}

uint64_t timer_ticks(void)
{
    return ticks;
}
