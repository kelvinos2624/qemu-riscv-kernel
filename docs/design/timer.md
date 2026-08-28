# Timer Design

## Scope

The kernel runs in S-mode, while QEMU `virt` exposes the hardware timer as a
machine-level device. A small M-mode shim owns the machine timer compare
register. The S-mode timer driver owns tick accounting, scheduler wakeups, and
preemption policy.

## Hardware Path

QEMU `virt` exposes the machine timer through CLINT/ACLINT-compatible MMIO:

- `mtime`: `0x0200bff8`
- hart 0 `mtimecmp`: `0x02004000`

S-mode reads `mtime` through the identity-mapped CLINT page. It does not write
`mtimecmp` directly. Instead, S-mode issues a machine `ecall` with an absolute
deadline in the same `mtime` timebase. M-mode programs `mtimecmp` to exactly
that deadline.

The current `TIMER_INTERVAL_CYCLES` value is 10,000. QEMU `virt` exposes a
10 MHz timebase, so this produces an approximate 1 ms kernel tick. This is a
platform assumption until the kernel grows device-tree timebase discovery.

## Interrupt Flow

Timer setup enables:

- `mie.MTIE` in M-mode so the machine timer can reach the shim
- `mideleg.STIP` so reflected supervisor timer interrupts enter S-mode
- `sie.STIE` for supervisor timer interrupts
- `sstatus.SIE` for normal S-mode interrupt delivery

When the machine timer fires, M-mode handles only platform mechanism:

1. set `mtimecmp` to `UINT64_MAX` so MTIP deasserts
2. set the supervisor timer pending bit
3. return to S-mode

The S-mode trap handler receives the supervisor timer interrupt. It increments
the monotonic tick counter, computes the next absolute deadline, asks M-mode to
program that deadline, wakes expired sleepers, and requests scheduler preemption
when the current quantum expires.

M-mode must not call scheduler, wait queue, mutex, heap, page allocator, trace,
or address-space policy code.

## Handler Policy

The S-mode timer handler is intentionally short. It increments a monotonic tick
counter, schedules the next compare value through the M-mode shim, and calls the
scheduler tick hook. It does not print to UART, pick a thread, manipulate the
ready queue directly, or perform the context switch itself. Expired sleepers are
handled by scheduler code through `thread_on_timer_tick()`.

This mirrors the ECE350 SysTick lesson: one hardware timer provides periodic
kernel control, while software tracks per-task timeslices, wakeups, and
deadlines.

The scheduler quantum is `THREAD_QUANTUM_TICKS` ticks. With the current 1 ms
tick and a 10 tick quantum, preemptive round-robin uses an approximate 10 ms
time slice.

## Next Work

- Decide whether scheduler time accounting uses fixed ticks or absolute
  timestamps.
- Replace fixed CLINT addresses with device-tree discovery.
