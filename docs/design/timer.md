# Timer Design

## Scope

This milestone configures the RISC-V machine timer on QEMU `virt`, enables
machine-timer interrupts, handles them through the existing trap path, and
maintains a monotonic kernel tick counter.

## Hardware Path

QEMU `virt` exposes the machine timer through CLINT/ACLINT-compatible MMIO:

- `mtime`: `0x0200bff8`
- hart 0 `mtimecmp`: `0x02004000`

The timer driver programs `mtimecmp` to `mtime + TIMER_INTERVAL_CYCLES`, then
reprograms it on each timer interrupt.

## Interrupt Enable Order

The kernel installs `mtvec` and passes the trap self-test before enabling timer
interrupts. This prevents the machine timer from firing before there is a valid
trap entry.

Timer setup enables:

- `mie.MTIE` for machine-timer interrupts
- `mstatus.MIE` for global machine-mode interrupts

## Handler Policy

The timer interrupt handler is intentionally short. It increments a monotonic
tick counter and schedules the next compare value. It does not print to UART or
run scheduler policy yet.

This mirrors the ECE350 SysTick lesson: one hardware timer provides periodic
kernel control, while software tracks per-task timeslices, wakeups, and
deadlines.

## Next Work

- Add a sleep queue based on timer ticks.
- Use timer interrupts to preempt the running thread.
- Decide whether scheduler time accounting uses fixed ticks or absolute
  timestamps.
