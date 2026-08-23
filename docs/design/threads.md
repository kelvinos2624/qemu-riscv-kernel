# Thread Design

## Scope

This milestone adds cooperative kernel threads. The kernel can initialize a
thread subsystem, create threads, start scheduling, voluntarily yield between
threads, and retire exited threads.

Timer interrupts continue to run, but they do not preempt threads yet.

All threads in this milestone run in RISC-V machine mode. They are kernel
threads, not user tasks: there is no address-space separation, user stack,
`sret` path, page-table switch, or syscall boundary yet.

## Public API

```c
void thread_init(void);
int thread_create(const char *name, void (*entry)(void *), void *arg);
void thread_start(void);
void thread_yield(void);
void thread_exit(void);
tid_t thread_current_tid(void);
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

Threads use a static table, static kernel stacks, and a bounded FIFO ready
queue:

- `THREAD_MAX`: 8, including the null task
- `THREAD_STACK_SIZE`: 4096 bytes
- ready queue capacity: `THREAD_MAX - 1`

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

Only `THREAD_QUEUE_READY` is active in this milestone. The sleep and wait
members are reserved so future sleep queues and wait queues can use the same
"one queue owner per thread" invariant instead of adding ad hoc duplicate
checks later.

## Context Switching

Cooperative switches use `context_switch(uintptr_t *old_sp, uintptr_t new_sp)`.
The assembly routine saves:

- `ra`
- `s0` through `s11`
- the resulting stack pointer

This is enough for cooperative kernel threads because `thread_yield()` is a
normal C call and the RISC-V ABI already treats `a0-a7` and `t0-t6` as
caller-saved.

This is intentionally separate from trap handling. Trap entry saves fuller
machine state because interrupts and exceptions can happen between arbitrary
instructions.

## Initial Thread Stack

A newly created thread has never called `context_switch()`, so
`thread_create()` builds a fake switch frame on the new thread's stack. The
saved `ra` points at `thread_trampoline()`.

On first schedule:

1. `context_switch()` loads the new stack.
2. It restores `ra` from the fake frame.
3. `ret` enters `thread_trampoline()`.
4. The trampoline calls the thread entry function.
5. If the entry function returns, the trampoline calls `thread_exit()`.

## Scheduler Policy

The scheduler currently uses cooperative FIFO round-robin over a bounded ready
queue. The queue contains real runnable threads only. TID 0, the null task, is
never enqueued and is selected only when the ready queue is empty.

Queue semantics:

- `thread_create()` marks a new real thread `THREAD_READY` and appends it to the
  ready queue tail.
- `thread_yield()` marks the current real running thread `THREAD_READY`, appends
  it to the ready queue tail, then pops the next TID from the ready queue head.
- `thread_exit()` marks the current real thread `THREAD_EXITED` and does not
  requeue it.
- if the ready queue is empty, `pick_next_thread()` returns the null task.

This makes the tie-break rule arrival order into the ready queue, not static TID
order. It is the scheduler shape needed for later timer preemption: a preempted
thread can become ready, re-enter at the tail, and the next runnable thread can
be selected from the head.

Thread states for this milestone:

- `THREAD_UNUSED`
- `THREAD_READY`
- `THREAD_RUNNING`
- `THREAD_EXITED`

Blocking and sleeping states come later.

## Interrupt Safety

Scheduler state mutations are protected with `irq_save()` and `irq_restore()`.
This prevents a machine-timer interrupt from observing or acting on partial
scheduler state during `thread_create()`, `thread_yield()`, or `thread_exit()`.

For this uniprocessor milestone, masking interrupts is sufficient. A future SMP
kernel would need spinlocks in addition to interrupt masking.

## Next Work

- Add blocked and sleeping states.
- Add a sleep queue driven by timer ticks.
- Add preemptive round-robin from the machine-timer interrupt.
- Add scheduler tracing for context switch events.
