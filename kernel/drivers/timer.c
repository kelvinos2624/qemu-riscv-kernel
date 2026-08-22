#include "arch/riscv64/csr.h"
#include "core/kernel.h"
#include "drivers/timer.h"

static volatile uint64_t ticks;
static uint64_t next_compare;

static inline uint64_t mmio_read64(uintptr_t addr)
{
    return *(volatile uint64_t *)addr;
}

static inline void mmio_write64(uintptr_t addr, uint64_t value)
{
    *(volatile uint64_t *)addr = value;
}

static uint64_t timer_read_mtime(void)
{
    return mmio_read64(TIMER_CLINT_MTIME);
}

static void timer_write_mtimecmp(uint64_t value)
{
    mmio_write64(TIMER_CLINT_MTIMECMP, value);
}

void timer_init(void)
{
    ticks = 0;
    next_compare = timer_read_mtime() + TIMER_INTERVAL_CYCLES;
    timer_write_mtimecmp(next_compare);

    csr_set_mie(MIE_MTIE);
    csr_set_mstatus(MSTATUS_MIE);

    console_write("timer: interval=");
    console_write_hex64(TIMER_INTERVAL_CYCLES);
    console_write(" mie=");
    console_write_hex64(csr_read_mie());
    console_write("\n");
}

void timer_handle_interrupt(void)
{
    const uint64_t now = timer_read_mtime();

    ticks++;

    do {
        next_compare += TIMER_INTERVAL_CYCLES;
    } while (next_compare <= now);

    timer_write_mtimecmp(next_compare);
}

uint64_t timer_ticks(void)
{
    return ticks;
}
