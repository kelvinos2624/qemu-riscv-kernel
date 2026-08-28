# DDR 13: Mutexes with FIFO Owner Transfer

## Status

Accepted.

Follow-up: DDR 21 migrates normal kernel execution to S-mode. The mutex design
still relies on short `irq_save()` / `irq_restore()` critical sections, but
those helpers now mask `sstatus.SIE`; M-mode remains a policy-free platform
shim.

## Context

Wait queues provide a reusable way to block and wake threads, but they do not
own the condition being waited on. The next synchronization primitive needs to
own both the condition and the wait queue so the kernel can avoid lost wakeups
at the check-and-block boundary.

The first higher-level primitive is a kernel mutex. It must support blocking
acquisition, owner-only unlock, and a clear fairness rule that matches the
current scheduler.

## Decision

Add `kernel/core/sync.h` and `kernel/core/sync.c` for synchronization policy.
Keep wait queue mechanics in `thread.c`.

Introduce `mutex_t` with:

- `owner`
- embedded FIFO `waiters`
- debug `name`

Use `MUTEX_NO_OWNER` as the unlocked sentinel instead of TID 0. TID 0 is the
null task and should never be confused with an unlocked mutex owner.

Mutexes are non-recursive. Recursive lock attempts panic. Unlock by a non-owner
also panics.

`mutex_unlock()` wakes at most one waiter. If a waiter exists, ownership is
transferred to that waiter before it runs. If no waiter exists, the mutex
becomes unlocked.

Timed mutex waits are added later by composing this owner-transfer policy with
wait queue timeout cancellation.

`wait_queue_wake_one()` now returns the TID it woke, or `THREAD_INVALID_TID`
when the queue was empty. This keeps owner-transfer policy in `sync.c` without
exposing wait queue internals.

## Alternatives Considered

### Release to Unlocked, Then Wake

The simplest mutex unlock path is:

1. set owner to no owner
2. wake one waiter

This permits lock stealing. An unrelated runnable thread may acquire the mutex
before the oldest waiter resumes, so FIFO wait order becomes advisory rather
than meaningful.

### Recursive Mutex

A recursive mutex could track an ownership depth. That was rejected because the
kernel needs ordinary mutex semantics first, and accidental recursion is more
likely to hide a synchronization bug than express intended design.

### Keep Mutexes in `thread.c`

This would avoid a new source file, but it mixes synchronization policy with
scheduler mechanics. A separate `sync.c` gives future semaphores, condition
variables, and spinlocks a natural home.

## Consequences

FIFO owner transfer matches the current preemptive round-robin scheduler and
makes the demo deterministic.

This policy may need to change once the scheduler grows priorities, EDF, or
MLFQ. In those schedulers, selecting the oldest waiter may be less appropriate
than selecting the highest-priority waiter, earliest-deadline waiter, or a
waiter chosen by queue-level policy. The current implementation keeps that
future refactor small by relying on the wait queue wake API instead of reaching
into scheduler internals.

Owner transfer does not solve priority inversion. It only decides who receives
the mutex after unlock. A future priority-inheritance experiment can build on
the explicit owner field to boost the current owner while higher-priority
waiters exist.

The implementation masks interrupts around mutex owner checks and wait-queue
entry. On the current single-hart machine-mode kernel, this protects the
check-and-block critical section and avoids lost wakeups. This is a short
kernel critical section, not a substitute for blocking synchronization.

Threads must release mutexes before exiting. Robust owner-death cleanup is out
of scope because it would require tracking the set of mutexes owned by each
thread.

This maps to ECE350 and the STM32 RTOS lab model: a task that cannot acquire a
mutex leaves the ready queue, waits on the mutex-owned blocked list, and returns
to READY when the mutex is released.

## Evidence

The QEMU smoke test now verifies a contended mutex sequence:

1. thread A acquires the mutex
2. thread B attempts to acquire and blocks
3. thread A unlocks
4. thread B resumes as the transferred owner
5. the null task runs after both real threads exit
