# DDR 12: Use Wait Queues for Blocking Events

## Status

Accepted

## Context

The kernel can preempt threads and put them to sleep until a future tick, but it
does not yet have a generic way for a thread to block until another kernel path
signals an event. Mutexes, driver completion, pipes, and condition-style waits
all need the same core transition:

```text
RUNNING -> BLOCKED -> READY
```

## Decision

Introduce `wait_queue_t` as an embeddable kernel object. Each wait queue stores
TIDs blocked on one logical condition owned by some subsystem.

The wait queue does not store or evaluate the condition. The owning subsystem
does that. For example:

- a mutex owns "the lock became available"
- a driver request owns "the request completed"
- a pipe owns "data became readable"

Waiters are stored in FIFO order. `wait_queue_wake_one()` wakes the oldest
waiter and returns its TID, and `wait_queue_wake_all()` wakes every waiter in
FIFO order. Woken threads are appended to the ready queue tail; the ready queue
remains responsible for dispatch order.

## Alternatives Considered

- A single global event queue.
- Condition-specific scheduler code for each subsystem.
- Implement mutexes directly without a reusable wait primitive.

## Consequences

Using one wait queue per condition keeps FIFO ordering meaningful. A global
queue mixing unrelated conditions would make wake order ambiguous and would
require every wakeup to filter waiters by event type.

Embedding wait queues in future kernel objects scales naturally with the number
of objects. A mutex can contain one wait queue, a driver request can contain one
completion wait queue, and a pipe can contain separate readable/writable wait
queues.

The first implementation uses fixed-size TID storage, matching the current
static thread table and avoiding allocator dependencies.

The important caveat is lost wakeup avoidance. The correct usage pattern is:

```c
while (!condition) {
    wait_queue_sleep(&queue);
}
```

The condition check and transition to blocked must be protected by the
synchronization primitive that owns the condition. This milestone does not yet
provide spinlocks or mutexes, so it documents that contract and uses a
deterministic demo ordering. The next synchronization work should make that
check-and-sleep boundary explicit.

This maps to the ECE350/STM32 RTOS model where a task leaves the ready list
while blocked on a kernel event, then returns to READY when the event is
signaled.

## Evidence

The QEMU smoke test creates two waiters blocked on the same demo event queue.
A signaler sets the event and calls `wait_queue_wake_one()` twice. The test
verifies that waiter A resumes before waiter B, proving FIFO wait order for one
condition.
