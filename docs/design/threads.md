# Thread Design

## Scope

This milestone adds machine-mode kernel threads with FIFO round-robin
scheduling. The kernel can initialize a thread subsystem, create threads, start
scheduling, voluntarily yield between threads, preempt CPU-bound threads from
the timer interrupt path, put threads to sleep until a future tick, and retire
exited threads. It also supports FIFO wait queues for blocking on kernel-owned
events.

All threads in this milestone run in RISC-V machine mode. They are kernel
threads, not user tasks: there is no address-space separation, user stack,
`sret` path, page-table switch, or syscall boundary yet.

## Public API

```c
typedef struct wait_queue wait_queue_t;

void thread_init(void);
int thread_create(const char *name, void (*entry)(void *), void *arg);
void thread_start(void);
void thread_yield(void);
void thread_sleep(uint64_t ticks);
void thread_exit(void);
tid_t thread_current_tid(void);
void wait_queue_init(wait_queue_t *queue, const char *name);
void wait_queue_sleep(wait_queue_t *queue);
void wait_queue_wake_one(wait_queue_t *queue);
void wait_queue_wake_all(wait_queue_t *queue);
```

The shape intentionally resembles the ECE350 RTX split between kernel
initialization, task creation, kernel start, yield, exit, and current-task ID.
The names are kernel-oriented rather than course API names.

Thread identity uses `tid_t`, a kernel-owned 16-bit TID type. The current static
thread table is still limited to `THREAD_MAX`, but the identity type is separate
from the implementation's table index and from C's generic `int` return codes.

## Null Task

Thread ID 0 is reserved for the null task. It is created during
`thread_init()`, never exits, and runs only when no real thread is runnable.

The null task body idles with `wfi` and then calls `thread_yield()` after an
interrupt wakes the CPU. This separates scheduler policy from the CPU idle
mechanism:

- null task: scheduler-visible idle thread
- `wfi`: RISC-V instruction that sleeps until an interrupt

This keeps the scheduler invariant simple: `pick_next_thread()` always returns
a thread. Re-entering the scheduler after `wfi` also prepares the idle path for
future timer-driven wakeups.

The null task prints a one-time idle banner when it first runs so the QEMU smoke
test can prove that exited real threads fall back to the idle path.

## Thread Storage

Threads use a static table, static kernel stacks, a bounded FIFO ready queue, a
bounded sleep queue, and embeddable wait queues:

- `THREAD_MAX`: 8, including the null task
- `THREAD_STACK_SIZE`: 4096 bytes
- ready queue capacity: `THREAD_MAX - 1`
- sleep queue capacity: `THREAD_MAX - 1`
- wait queue capacity: `THREAD_MAX - 1`

Static stacks keep this milestone deterministic and independent of the memory
allocator milestone. Dynamic stack allocation should be introduced after the
kernel has a heap or page allocator.

The ready queue stores TIDs rather than `thread_t *` pointers. The TCB table
owns thread storage; queues own scheduling order.

Each TCB also tracks its current queue owner:

- `THREAD_QUEUE_NONE`
- `THREAD_QUEUE_READY`
- `THREAD_QUEUE_SLEEP`
- `THREAD_QUEUE_WAIT`

`THREAD_QUEUE_READY`, `THREAD_QUEUE_SLEEP`, and `THREAD_QUEUE_WAIT` are active.
The same "one queue owner per thread" invariant prevents a thread from being
ready, sleeping, and event-blocked at the same time.

## Context Switching

Thread switches now use trap-frame ownership. Each thread has a saved
`trap_frame_t *` in its TCB. When a thread is interrupted, yields, or exits
through the machine-mode `ecall` path, the trap entry has already saved the full
interrupted machine state. The scheduler can keep that frame on the outgoing
thread's stack and return a different thread's frame to the assembly restore
path.

The timer interrupt handler does not call `thread_yield()` and does not run
ready-queue policy directly. It updates timer state and notifies the scheduler
tick hook. The trap handler then checks whether returning to a different thread
is allowed.

This mirrors the ECE350/STM32 RTOS split between SysTick and PendSV: the timer
interrupt provides periodic control, while the actual context switch is deferred
to a controlled exception-return boundary. RISC-V does not provide PendSV here,
so the kernel uses the trap return path as that boundary.

## Initial Thread Stack

A newly created thread has never trapped or been preempted, so
`thread_create()` builds a synthetic trap frame on the new thread's stack. The
saved `mepc` points at `thread_trampoline()`, and the saved `sp` points at the
top of the thread's kernel stack.

On first schedule:

1. `trap_restore()` loads the selected trap frame.
2. It restores `mepc`, `mstatus`, general-purpose registers, and `sp`.
3. `mret` enters `thread_trampoline()`.
4. The trampoline calls the thread entry function.
5. If the entry function returns, the trampoline calls `thread_exit()`.

## Scheduler Policy

The scheduler uses FIFO round-robin over a bounded ready queue. The queue
contains real runnable threads only. TID 0, the null task, is never enqueued and
is selected only when the ready queue is empty.

Queue semantics:

- `thread_create()` marks a new real thread `THREAD_READY` and appends it to the
  ready queue tail.
- `thread_yield()` enters the trap path with a machine-mode `ecall`; the trap
  scheduler marks the current real running thread `THREAD_READY`, appends it to
  the ready queue tail, then pops the next TID from the ready queue head.
- timer ticks increment the current thread's quantum counter. Once it reaches
  `THREAD_QUANTUM_TICKS`, the scheduler requests preemption.
- `thread_sleep(ticks)` enters the trap path with a machine-mode `ecall`. For a
  positive tick count, the current real thread becomes `THREAD_SLEEPING`, stores
  an absolute `wake_tick`, enters the sleep queue, and the scheduler selects the
  next ready thread.
- `thread_sleep(0)` behaves like `thread_yield()` for real threads.
- `thread_exit()` marks the current real thread `THREAD_EXITED` and does not
  requeue it.
- if the ready queue is empty, `pick_next_thread()` returns the null task.

This makes the tie-break rule arrival order into the ready queue, not static TID
order. A preempted thread becomes ready, re-enters at the tail, and the next
runnable thread is selected from the head.

`THREAD_QUANTUM_TICKS` is 10. With the current 1 ms QEMU timer tick, the
scheduler quantum is approximately 10 ms.

## Sleep Queue

The sleep queue uses absolute wake ticks:

```c
wake_tick = timer_ticks() + ticks;
```

Sleeping threads are kept in a sorted fixed array of TIDs. The head has the
earliest `wake_tick`, and same-deadline sleepers keep insertion order. On each
timer tick, `thread_on_timer_tick()` wakes expired sleepers from the head:

1. `THREAD_SLEEPING -> THREAD_READY`
2. `THREAD_QUEUE_SLEEP -> THREAD_QUEUE_NONE`
3. append to the ready queue tail

The timer driver does not manipulate the sleep queue directly. It updates
hardware timer state and calls the scheduler tick hook.

Absolute wake ticks were chosen over a delta queue because they are easier to
trace, test, and compose with future timeout work. A delta queue is common in
small tick-driven MCU RTOS kernels, but absolute deadlines make telemetry more
direct: traces can log `now`, `wake_tick`, requested duration, and wakeup
lateness without reconstructing cumulative deltas.

The sorted array is intentionally bounded and simple for `THREAD_MAX = 8`. If
the kernel grows substantially more sleepers, the likely upgrade path is a
min-heap keyed by `wake_tick` or a timer wheel, not a larger sorted array.

## Wait Queues

A wait queue is an embeddable kernel object that stores threads blocked on one
specific condition owned by another subsystem. The wait queue is not the event
itself; it is the place where threads sleep while waiting for that event.

Examples of future owners:

- `mutex.waiters`: mutex became available
- `request.done_waiters`: driver request completed
- `pipe.readable`: pipe contains data

Each wait queue should represent one logical condition. A single global queue
for unrelated events would make FIFO wake order meaningless.

Current wait queue semantics:

- `wait_queue_sleep(queue)` enters the trap path with a machine-mode `ecall`.
- the current real thread becomes `THREAD_BLOCKED`.
- the thread enters `queue` in FIFO order with `THREAD_QUEUE_WAIT` ownership.
- `wait_queue_wake_one(queue)` removes the oldest waiter and appends it to the
  ready queue tail.
- `wait_queue_wake_all(queue)` repeats that until the queue is empty.

Wakeup means "the condition may now be true", not "the condition is guaranteed
for this thread." Callers should use the standard pattern:

```c
while (!condition) {
    wait_queue_sleep(&queue);
}
```

The condition check and the transition to blocked must eventually be protected
by the synchronization primitive that owns the condition. This milestone does
not yet provide spinlocks or mutexes, so the demo uses deterministic thread
ordering to avoid lost wakeups. The next mutex/spinlock work should make the
check-and-sleep boundary explicit.

This maps to the ECE350/STM32 RTOS model where a task leaves the ready list
while waiting for a kernel event, then re-enters READY when another kernel path
signals that event.

Thread states for this milestone:

- `THREAD_UNUSED`
- `THREAD_READY`
- `THREAD_RUNNING`
- `THREAD_BLOCKED`
- `THREAD_SLEEPING`
- `THREAD_EXITED`

Mutex ownership and timed waits come later.

## Interrupt Safety

Scheduler state mutations are protected with `irq_save()` and `irq_restore()`.
This prevents a machine-timer interrupt from observing or acting on partial
scheduler state during ready-queue and TCB transitions.

The kernel also tracks a small `preempt_disable_depth`. Timer ticks continue to
increment while preemption is disabled, but the scheduling effect is deferred
until preemption is allowed. In other words, the kernel disables preemption, not
timekeeping.

For this uniprocessor milestone, masking interrupts is sufficient. A future SMP
kernel would need spinlocks in addition to interrupt masking.

## Next Work

- Add mutexes.
- Add timed waits.
- Add scheduler tracing for context switch events.
