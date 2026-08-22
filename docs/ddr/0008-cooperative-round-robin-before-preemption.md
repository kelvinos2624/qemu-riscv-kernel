# DDR 8: Implement Cooperative Round-Robin Before Preemption

## Status

Accepted

## Context

The machine timer is already running, but switching threads from interrupt
context combines timer handling, scheduler policy, and context switching all at
once.

## Decision

Implement cooperative round-robin first. Timer interrupts continue to update
the monotonic tick counter, but they do not trigger scheduling yet.

## Alternatives Considered

- Add preemptive scheduling immediately.
- Implement deadline or priority scheduling first.
- Keep only one kernel control flow until memory management is complete.

## Consequences

Cooperative switching makes context-switch bugs easier to isolate because
switches occur at explicit `thread_yield()` and `thread_exit()` points. The
tradeoff is that CPU-bound threads must voluntarily yield until timer-driven
preemption is added.

This progression matches the ECE350 RTX lab path: cooperative task management
before SysTick-driven preemptive scheduling.

## Evidence

`kernel/core/main.c` creates two demo threads that alternate via
`thread_yield()` and then exit. The QEMU smoke test waits for the cooperative
thread milestone banner.
