# DDR 11: Use Absolute Wake Ticks for the Sleep Queue

## Status

Accepted

## Context

The kernel has timer-driven preemptive scheduling, but a thread that wants to
wait for time to pass still needs a real blocking primitive. Busy-waiting on
`timer_ticks()` wastes CPU time and keeps the thread runnable even though it has
no useful work to do.

The scheduler already tracks queue ownership in each TCB, including the reserved
`THREAD_QUEUE_SLEEP` owner. This milestone activates that path.

## Decision

Implement `thread_sleep(uint64_t ticks)` and a bounded sleep queue using
absolute wake ticks:

```c
wake_tick = timer_ticks() + ticks;
```

Sleeping threads are stored as TIDs in a sorted fixed array. The earliest
`wake_tick` is at the head. If multiple threads have the same wake tick, the
queue preserves insertion order.

`thread_sleep(0)` behaves like `thread_yield()` for real threads. The null task
is not allowed to sleep.

Expired sleepers are woken from `thread_on_timer_tick()`, not from the timer
driver. Waking a sleeper only makes it ready: the thread is appended to the
ready queue tail, and the ready queue remains responsible for dispatch order.

## Alternatives Considered

- Delta queue.
- Unsorted sleeper list scanned on every tick.
- Min-heap keyed by wake tick.
- Timer wheel or hierarchical timer wheel.

## Consequences

Absolute wake ticks are easy to trace and test. A future trace event can log
`now`, `wake_tick`, requested duration, and wakeup lateness directly without
reconstructing cumulative deltas from queue internals.

The sorted fixed array keeps the implementation deterministic and allocator-free
while `THREAD_MAX` is small. Insert and pop can shift array entries, but the
cost is bounded by `THREAD_MAX - 1`.

The design does not pretend to scale indefinitely. If thread count or timer
volume grows, the likely upgrade path is a min-heap keyed by absolute
`wake_tick`, or a timer wheel for high timer volume.

This maps to the ECE350/STM32 RTOS model where SysTick advances kernel time and
sleeping tasks are moved back to READY when their delay expires. The main
difference is representational: this kernel stores absolute wake deadlines
rather than a delta queue, favoring observability and future timeout tracing.

## Evidence

The QEMU smoke test creates two sleepers with different deadlines and a peer
thread. It verifies that both sleepers block, the peer runs while they are not
runnable, and the shorter absolute deadline wakes before the longer one.
