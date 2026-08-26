# DDR 14: Timeout-Aware Blocking

## Status

Accepted.

## Context

The kernel already has separate mechanisms for time-based blocking
(`thread_sleep()`), event-based blocking (`wait_queue_sleep()`), and
resource-based blocking (`mutex_lock()`). Timed waits need to compose those
mechanisms so a thread can block until either an event occurs or a timeout
expires.

The important design problem is queue ownership. A timed event waiter is
logically waiting on both a wait queue and a timeout deadline. The older single
queue-owner field could not represent that without special cases.

## Decision

Represent sleep and event waits as forms of `THREAD_BLOCKED` with an explicit
wait reason:

- `THREAD_WAIT_NONE`
- `THREAD_WAIT_SLEEP`
- `THREAD_WAIT_QUEUE`
- `THREAD_WAIT_QUEUE_TIMEOUT`

Replace the old single queue-owner enum with explicit membership flags in the
TCB:

- `in_ready_queue`
- `in_sleep_queue`
- `in_wait_queue`

A timed wait may have both `in_sleep_queue` and `in_wait_queue` set. Whichever
path wins cancels the other membership:

- event wake removes the timeout entry
- timeout expiry removes the wait-queue entry

Expose timeout-aware APIs:

```c
int wait_queue_sleep_timeout(wait_queue_t *queue, uint64_t ticks);
int mutex_lock_timeout(mutex_t *mutex, uint64_t ticks);
```

The return values are:

- `WAIT_OK`
- `WAIT_TIMEOUT`

`ticks == 0` does not block. Infinite waits stay on the existing blocking APIs
instead of using a magic forever value.

If event wake and timeout expiry are both possible at the same tick, execution
order decides the result. The first kernel path to process the TCB wins.

## Alternatives Considered

### Keep One Queue Owner

The kernel could keep one queue owner and special-case timed waits. This keeps
the TCB smaller, but it makes cancellation awkward because timed waits really
belong to two scheduling structures.

### Duplicate Timeout State Outside the TCB

Another option is to store timeout metadata in a separate table and leave the
TCB mostly unchanged. That hides dual membership rather than modeling it, and
future driver-completion timeouts would need to rediscover the same linkage.

### Use a Timer Wheel Now

A hierarchical timer wheel or heap would scale better for large numbers of
timed waits. That is unnecessary for `THREAD_MAX = 8` and would add more
infrastructure before the kernel has enough workloads to measure.

## Consequences

Timed waits now compose naturally with mutexes and future driver completions.
A blocked thread can carry a wait reason, a wait queue pointer, an absolute
deadline, and a wake result.

Cancellation uses linear scans over bounded arrays. That cost is acceptable
because the kernel has a static thread limit and cancellation is not on the
uncontended mutex fast path. If future workloads create many timed waits, the
documented upgrade path is intrusive queue links, a min-heap with cancellation
handles, or a timer wheel.

The null task can run in the middle of the timed-wait demo because all real
threads are blocked. That is expected behavior and is useful evidence that the
scheduler does not busy-wait for deadlines.

This maps to ECE350 and the STM32 RTOS model for timed blocking calls such as
mutex acquire with timeout or blocking receive with timeout: a task leaves the
ready queue, waits on an event, and either resumes because the event occurred or
because the system tick reached its deadline.

## Evidence

The QEMU smoke test verifies:

1. thread A owns a mutex and sleeps
2. thread B waits on that mutex with a short timeout
3. the null task runs while all real threads are blocked
4. thread B wakes with `WAIT_TIMEOUT`
5. thread A unlocks later
6. thread C acquires the mutex successfully

This proves the timed-out waiter was removed from the mutex wait queue and did
not receive ownership after its timeout.
