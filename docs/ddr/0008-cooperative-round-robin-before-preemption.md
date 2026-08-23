# DDR 8: Implement Cooperative Round-Robin Before Preemption

## Status

Accepted; scheduler policy superseded by DDR 9

## Context

The machine timer is already running, but switching threads from interrupt
context combines timer handling, scheduler policy, and context switching all at
once.

## Decision

Implement cooperative circular TID round-robin first. Timer interrupts continue
to update the monotonic tick counter, but they do not trigger scheduling yet.

For this milestone, "round-robin" means a deterministic scan over non-null TIDs:
start after the current TID, wrap at the end of the static thread table, and
choose the first ready thread. This is intentionally not an explicit FIFO ready
queue yet.

## Alternatives Considered

- Add preemptive scheduling immediately.
- Implement deadline or priority scheduling first.
- Keep only one kernel control flow until memory management is complete.

## Consequences

Cooperative switching makes context-switch bugs easier to isolate because
switches occur at explicit `thread_yield()` and `thread_exit()` points. The
tradeoff is that CPU-bound threads must voluntarily yield until timer-driven
preemption is added.

The circular TID scan keeps the scheduler small while the context-switching path
is still new. The tradeoff is that ready order is tied to TID order, not arrival
order. A later blocking/preemption milestone should introduce a FIFO ready queue
so yielding, waking, and newly created threads can all define tail-insertion
semantics explicitly.

This progression matches the ECE350 RTX lab path: cooperative task management
before SysTick-driven preemptive scheduling.

## Evidence

`kernel/core/main.c` creates three demo threads that rotate via `thread_yield()`
and then exit. The QEMU smoke test verifies the UART-observed order
`A0, B0, C0, A1, B1, C1, A2, B2, C2`, which demonstrates the circular TID
tie-break and wraparound behavior. It also waits for the one-time null-task idle
banner, proving that `thread_exit()` falls back to the idle path after real
threads retire.
