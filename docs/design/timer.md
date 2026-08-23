# Timer Design

## Scope

This milestone configures the RISC-V machine timer on QEMU `virt`, enables
machine-timer interrupts, handles them through the existing trap path, and
maintains a monotonic kernel tick counter. The timer also drives scheduler
quantum accounting.

## Hardware Path

QEMU `virt` exposes the machine timer through CLINT/ACLINT-compatible MMIO:

- `mtime`: `0x0200bff8`
- hart 0 `mtimecmp`: `0x02004000`

The timer driver programs `mtimecmp` to `mtime + TIMER_INTERVAL_CYCLES`, then
reprograms it on each timer interrupt.

The current `TIMER_INTERVAL_CYCLES` value is 10,000. QEMU `virt` exposes a
10 MHz timebase, so this produces an approximate 1 ms kernel tick. This is a
platform assumption until the kernel grows device-tree timebase discovery.

## Interrupt Enable Order

The kernel installs `mtvec` and passes the trap self-test before enabling timer
interrupts. This prevents the machine timer from firing before there is a valid
trap entry.

Timer setup enables:

- `mie.MTIE` for machine-timer interrupts
- `mstatus.MIE` for global machine-mode interrupts

## Handler Policy

The timer interrupt handler is intentionally short. It increments a monotonic
tick counter, schedules the next compare value, and calls the scheduler tick
hook. It does not print to UART, pick a thread, manipulate the ready queue, or
perform the context switch itself.

This mirrors the ECE350 SysTick lesson: one hardware timer provides periodic
kernel control, while software tracks per-task timeslices, wakeups, and
deadlines.

The scheduler quantum is `THREAD_QUANTUM_TICKS` ticks. With the current 1 ms
tick and a 10 tick quantum, preemptive round-robin uses an approximate 10 ms
time slice.

## Next Work

- Add a sleep queue based on timer ticks.
- Decide whether scheduler time accounting uses fixed ticks or absolute
  timestamps.
