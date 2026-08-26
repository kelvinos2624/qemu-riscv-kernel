# DDR 15: In-Memory Scheduler Tracing

## Status

Accepted.

## Context

The scheduler now supports preemption, sleep queues, wait queues, mutexes, and
timeout-aware blocking. The next milestones need observability, especially for
priority-inversion experiments where the interesting behavior is the ordering
of context switches, blocking, wakeups, and mutex ownership changes.

Printing directly to UART inside those paths would be simple, but it would also
make tracing part of the timing behavior being observed.

## Decision

Add `kernel/core/trace.h` and `kernel/core/trace.c`.

Use a fixed-size in-memory ring buffer as the primary trace sink. Add an
explicit `trace_dump()` API that formats the current buffer to UART only when
called by demo or debug code.

Trace records include:

- sequence number
- kernel tick
- event type
- primary TID
- related TID
- one integer argument

Tracing is compiled in by default through `CONFIG_TRACE ?= 1`. When
`CONFIG_TRACE=0`, trace APIs compile to no-op stubs so call sites remain clean
and scheduler correctness cannot depend on tracing side effects.

`trace_emit()` protects its own ring-buffer update with `irq_save()` and
`irq_restore()`.

## Alternatives Considered

### UART-Only Tracing

UART-only tracing is easy to inspect, but it is too slow and too intrusive for
scheduler hot paths. It can distort the behavior it is meant to explain.

### In-Memory Only

In-memory-only tracing keeps hot paths clean, but the current project still
needs visible QEMU test evidence. An explicit UART dump gives that evidence
without printing during every scheduler transition.

### Panic on Trace Overflow

Panicking on overflow would make trace capacity part of kernel behavior. That is
not acceptable for observability. The trace buffer overwrites the oldest events
and records the overwrite count instead.

## Consequences

Trace emission is O(1), integer-only, and uses no dynamic allocation.

Events can be overwritten if the buffer fills. This is intentional: old trace
data is less important than preserving kernel behavior.

The ring buffer and text dump are single-hart designs. A future SMP kernel would
likely need per-CPU buffers or stronger locking.

The dump path writes to UART with interrupts masked. That is acceptable because
dumping is explicit debug/test behavior, not a scheduler hot path.

## Evidence

The QEMU smoke test validates the existing timed-wait demo and checks that the
trace dump includes key event types:

- `context_switch`
- `wait_timeout`
- `mutex_timeout`
- `idle`

This proves the kernel records the scheduling evidence needed for the next
priority-inversion milestone.
