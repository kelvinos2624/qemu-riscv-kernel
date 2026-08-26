# DDR 16: Defer Priority Inversion Until Priority Scheduling

## Status

Accepted.

## Context

The planned scheduling and synchronization work included priority-inversion
experiments. The kernel now has the surrounding primitives needed to make that
topic meaningful:

- preemptive round-robin scheduling
- wait queues
- mutexes with owner tracking
- timeout-aware blocking
- scheduler tracing

However, the current scheduler is still FIFO round-robin. It has no thread
priority model, no priority-aware ready queues, and no priority-aware mutex
waiter selection.

Priority inversion is specifically a priority-scheduling problem. It requires a
high-priority thread, a lower-priority resource owner, and medium-priority work
that can preempt or outrank the owner. Without scheduler-visible priorities,
there is no priority relationship to invert.

## Decision

Do not implement a fake priority-inversion demo on top of the current
round-robin scheduler.

Document priority inversion as a known future scheduling risk and defer the
experiment until the kernel has a real priority scheduler.

The current scheduling and synchronization section is complete for the
round-robin scope. The next major project section should move to virtual memory
and allocation.

## Alternatives Considered

### Name Threads High, Medium, and Low

The kernel could create threads named high, medium, and low, then arrange a
mutex-blocking sequence that resembles priority inversion. This was rejected
because the scheduler would still treat all real threads equally. The result
would be a scripted ordering demo, not a priority-inversion experiment.

### Add Priorities Immediately

Adding priorities now would make priority inversion demonstrable, but it would
also expand the scheduling section into a new scheduler design:

- base and effective priorities
- priority-aware ready queues
- priority-aware wait queues
- priority inheritance or priority ceiling
- priority restoration across nested mutex ownership

That is useful future work, but it is a separate scheduler extension rather
than a small closeout to the current round-robin system.

### Implement Priority Inheritance Without a Priority Scheduler

Priority inheritance has no meaningful scheduling effect if the scheduler does
not observe priorities. The mutex owner could store a boosted number, but the
ready selector would ignore it.

## Consequences

This preserves technical honesty. The project does not claim to demonstrate a
scheduler behavior it does not implement.

The mutex owner field and trace subsystem still prepare the kernel for future
work. Once priorities exist, traces can show:

- high-priority thread blocks on a mutex
- low-priority owner is delayed by medium-priority work
- priority inheritance boosts the owner
- owner releases the mutex sooner
- high-priority waiter resumes

Future priority-inversion work should start with a priority scheduler, then
make mutex waiter selection scheduler-aware. FIFO owner transfer is appropriate
for the current round-robin scheduler but should not be treated as a priority
policy.

## Course Connection

This matches the ECE350/STM32 RTOS framing: priority inversion is a consequence
of combining blocking synchronization with priority scheduling. A round-robin
kernel can block on mutexes, but priority inversion requires the scheduler to
distinguish task priorities.
