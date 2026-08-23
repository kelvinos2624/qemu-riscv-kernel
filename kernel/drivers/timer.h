#ifndef KERNEL_DRIVERS_TIMER_H
#define KERNEL_DRIVERS_TIMER_H

#include <stdint.h>

#define TIMER_CLINT_MTIMECMP ((uintptr_t)0x02004000)
#define TIMER_CLINT_MTIME ((uintptr_t)0x0200bff8)
/* QEMU virt exposes a 10 MHz timebase, so 10,000 cycles is 1 ms. */
#define TIMER_INTERVAL_CYCLES ((uint64_t)10000)

void timer_init(void);
void timer_handle_interrupt(void);
uint64_t timer_ticks(void);

#endif
