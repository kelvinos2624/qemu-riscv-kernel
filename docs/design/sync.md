# Synchronization Design

## Scope

The synchronization layer currently provides non-recursive kernel mutexes built
on top of wait queues. Mutexes are thread-context blocking primitives for kernel
threads. They are not spinlocks and are not intended for interrupt handlers.

This layer is intentionally separate from `thread.c`:

- `thread.c` owns scheduler mechanics, queue ownership, blocking, wakeup, and
  context switching.
- `sync.c` owns synchronization policy, including mutex ownership rules,
  recursive-lock detection, and owner transfer.

That split keeps wait queues reusable for future synchronization objects,
driver completions, and pipe-style blocking.

## Public API

```c
typedef struct mutex mutex_t;

void mutex_init(mutex_t *mutex, const char *name);
void mutex_lock(mutex_t *mutex);
int mutex_trylock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
```

## Mutex State

Each mutex stores:

- `owner`: the TID that owns the mutex, or `MUTEX_NO_OWNER`
- `waiters`: a FIFO wait queue for blocked threads
- `name`: a debug label

`MUTEX_NO_OWNER` is distinct from TID 0. TID 0 is the null task, not an
unlocked mutex owner.

## Semantics

Mutexes are non-recursive. If the owner calls `mutex_lock()` or
`mutex_trylock()` again, the kernel panics. This keeps ownership explicit and
avoids silently introducing recursive-lock semantics where ordinary mutex
semantics are expected.

Only the owner may unlock a mutex. Unlock by a non-owner is a kernel bug and
panics.

The null task cannot own or block on a mutex. Mutex APIs are for real kernel
thread context only.

Threads must release owned mutexes before exiting. The kernel does not yet
track a per-thread owned-mutex list, so it does not provide robust owner-death
cleanup.

`mutex_trylock()` is the non-blocking acquisition path:

- returns `1` if the mutex was acquired
- returns `0` if another thread owns it
- panics on recursive acquisition

## Blocking and Lost Wakeups

`mutex_lock()` disables interrupts around the owner check and the transition
into the wait queue. On this single-hart kernel, that closes the lost-wakeup
window between:

1. seeing that the mutex is unavailable
2. deciding to block
3. entering the mutex wait queue

The thread still blocks through the normal trap path. Interrupt masking is used
as a short scheduler-state critical section, not as the mutex implementation
itself.

This is the same design boundary as the ECE350/STM32 RTOS distinction between
short critical sections and blocking synchronization. A spinlock or interrupt
mask can protect small kernel invariants; a mutex blocks schedulable tasks while
they wait for a longer-lived resource.

## Owner Transfer

Unlock uses FIFO owner transfer:

1. the current owner calls `mutex_unlock()`
2. the mutex wakes the oldest waiter, if any
3. ownership transfers to that waiter before it runs
4. the waiter resumes from `mutex_lock()` already owning the mutex

This prevents lock stealing by unrelated runnable threads and makes mutex
fairness match the current FIFO round-robin scheduler.

The policy is intentionally documented as scheduler-dependent. If the kernel
later adds priorities, EDF, or MLFQ, the waiter-selection policy should move
toward a scheduler-aware queue instead of hardcoded FIFO ordering. For example,
a priority scheduler may select the highest-priority waiter rather than the
oldest waiter.

## Priority Inversion

Owner transfer does not solve priority inversion. Priority inversion happens
while a high-priority thread is blocked behind a lower-priority owner. Owner
transfer only controls which waiter receives the mutex after unlock.

The explicit owner field is still useful for future priority inheritance: the
kernel can identify which owner should be boosted while higher-priority waiters
exist, then restore or recompute priority on unlock.

## Future Work

- Add owner-death checks or per-thread owned-mutex tracking.
- Add timed mutex waits once wait queues support timeout composition.
- Replace FIFO waiter selection with scheduler-aware selection if priorities,
  EDF, or MLFQ are introduced.
- Add priority-inheritance experiments once thread priorities exist.

## Test Evidence

The QEMU smoke test creates two demo threads:

1. thread A locks the mutex and yields while holding it
2. thread B attempts to lock and blocks on the mutex wait queue
3. thread A unlocks
4. ownership transfers to thread B
5. thread B resumes and completes the milestone banner

The expected UART sequence proves both blocking and FIFO owner transfer for the
current round-robin scheduler.
