# Scheduler Tracing Design

## Scope

The kernel now has a small tracing subsystem for scheduler and synchronization
events. Trace events are written to an in-memory ring buffer in the hot path and
dumped to UART only when `trace_dump()` is called explicitly.

The goal is observability without turning UART printing into part of scheduler
behavior.

## Public API

```c
void trace_init(void);
void trace_emit(trace_type_t type, tid_t tid, tid_t other_tid, uint64_t arg0);
void trace_dump(void);
```

Tracing is compiled in by default:

```make
CONFIG_TRACE ?= 1
```

When `CONFIG_TRACE=0`, `trace_emit()` and `trace_dump()` compile to no-op inline
stubs. Call sites do not need preprocessor branches.

## Trace Sink

Trace events are first stored in a fixed-size ring buffer. UART output happens
only during an explicit dump.

This avoids formatting strings in scheduler, timer, wait queue, or mutex hot
paths. It also keeps tracing closer to real kernel observability systems: record
compact events cheaply, export them later.

## Event Record

Each event stores:

- `seq`: monotonic sequence number
- `tick`: current kernel tick from `timer_ticks()`
- `type`: event type
- `tid`: primary thread ID
- `other_tid`: related thread ID, or `THREAD_INVALID_TID`
- `arg0`: event-specific integer metadata

The timestamp gives approximate time at 1 ms granularity. The sequence number
provides total ordering for events that happen in the same tick.

## Buffer Policy

The trace buffer has fixed capacity. When full, new events overwrite the oldest
events and increment an overwrite counter.

Tracing must not panic or block because the trace buffer filled. Dropping old
observability data is better than changing kernel behavior.

## Interrupt Safety

`trace_emit()` uses `irq_save()` and `irq_restore()` internally. This makes it
safe to call from scheduler, timer, wait queue, and synchronization code without
requiring each caller to remember the current interrupt state.

The tradeoff is a small amount of extra overhead on each trace call. That is
acceptable for this milestone and keeps the API hard to misuse.

## Dump Format

Trace dump output is stable structured text:

```text
trace: begin count=0x... overwrites=0x...
trace: seq=0x... tick=0x... type=context_switch tid=0x... other=0x... arg0=0x...
trace: end
```

The hot path stores integer event records. String formatting only happens during
`trace_dump()`.

## Current Events

The current trace types cover:

- thread creation
- context switches
- thread exit
- sleep and wake
- wait queue block, wake, and timeout
- mutex lock, block, timeout, and unlock
- idle task entry

This is enough to explain the current timed-wait demo and prepare for
priority-inversion experiments.

## Testing

The QEMU smoke test still validates the human-readable demo sequence exactly.
For trace output, it checks that important trace event types appear in the dump
instead of requiring a full exact trace transcript. This keeps tests useful
without making them brittle as harmless trace events are added.

## Course Connection

This mirrors the observability used when debugging RTOS scheduling in
ECE350/STM32-style labs: task switches, blocking points, timeout wakeups, and
idle execution are often the evidence needed to prove the scheduler behaved as
expected.
