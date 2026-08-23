# DDR 9: Replace Circular TID Scan with a Bounded Ready Queue

## Status

Accepted

## Context

The cooperative scheduler initially used a circular scan over static TIDs. That
was enough to validate the context-switch mechanism, but it tied scheduling
order to TID order instead of runnable arrival order.

The next scheduler milestones add timer preemption, sleep queues, wait queues,
and mutex blocking. Those features need a clearer ownership model: a thread
should be running, ready, sleeping, blocked, exited, or unused, and it should be
owned by at most one scheduler queue at a time.

## Decision

Use a bounded FIFO ready queue that stores `tid_t` values. The queue capacity is
`THREAD_MAX - 1` because TID 0 is the null task and is never enqueued.

Add queue ownership metadata to each TCB:

- `THREAD_QUEUE_NONE`
- `THREAD_QUEUE_READY`
- `THREAD_QUEUE_SLEEP`
- `THREAD_QUEUE_WAIT`

Only the ready queue is active in this milestone. Sleep and wait ownership
values reserve the invariant needed by upcoming sleep and blocking primitives.

## Alternatives Considered

- Keep the circular TID scan until timer preemption is implemented.
- Store `thread_t *` entries in the ready queue.
- Prevent duplicate ready entries by linearly scanning the ready queue on every
  enqueue.

## Consequences

FIFO ready-queue order gives textbook round-robin semantics: newly ready threads
enter at the tail, yielding or later preempted threads re-enter at the tail, and
the scheduler chooses from the head.

Storing TIDs keeps queue entries stable and easy to trace while the static TCB
table remains the owner of thread storage. Tracking queue ownership in the TCB
avoids repeated duplicate scans and prepares the same invariant for sleep and
wait queues.

The tradeoff is a slightly larger TCB and more scheduler bookkeeping before
timer preemption exists. This is intentional because the next milestone should
focus on interrupt-time switching mechanics rather than changing scheduler
policy at the same time.

This maps to the ECE350 RTX scheduling model: READY tasks live in a ready list,
blocked or sleeping tasks live elsewhere, and a task should not appear in two
scheduler-owned lists at once.

## Evidence

`kernel/core/thread.c` now implements a bounded TID ring buffer underneath
ready-specific scheduler helpers. The QEMU smoke test verifies the observed
FIFO rotation `A0, B0, C0, A1, B1, C1, A2, B2, C2`, then verifies fallback to
the null task after all real threads exit.
