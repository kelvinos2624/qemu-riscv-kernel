# DDR 33: User Task Lifecycle

## Status

Accepted

## Context

PR1 introduced task-owned user address spaces and a trampoline path. A
`user_task_t` owns its user page table, user code page, user stack page, and
supervisor-only trap-context page. A `thread_t` references that task while the
scheduler runs it.

PR1 intentionally deferred teardown. That avoided freeing the trap-context page
while the trap path was still proving the first `satp` switch, but it left no
real lifetime invariant for later syscalls, multiple user tasks, or failure
paths.

The key safety property is:

```text
Do not free a user task's address space or trap context while the current trap
return path may still inspect or return through that trap context.
```

## Decision

Add explicit user-task states:

```text
USER_TASK_UNUSED
USER_TASK_READY
USER_TASK_EXITED
USER_TASK_DESTROYED
```

`user/task.c` owns the lifecycle mechanism:

- initialize a task into `USER_TASK_READY`
- record the per-thread kernel stack pointer used by the trampoline
- mark a ready task exited with an exit code
- destroy only an exited task
- free the task-owned code, stack, trap-context, and page-table pages
- reject trap-frame and `satp` access for non-ready tasks

`thread.c` owns the cleanup timing policy. When a scheduled user thread exits,
the scheduler records the task exit, removes the task from the exiting TCB,
selects the next runnable thread, makes that thread current, and only then calls
`user_task_destroy()`.

This keeps the scheduler from becoming the owner of user-task memory while still
letting it enforce the one fact only it knows: when the exiting task is no
longer the current schedulable context.

## Consequences

`thread_create_user()` now accepts only ready user tasks. This means an exited or
destroyed task cannot be resumed accidentally by reusing an old `user_task_t`
pointer.

The design is deliberately smaller than a full process model. There is still no
parent/child relationship, wait status, reference counting, address-space
sharing, or dynamic executable loader. Those belong in later Stage 5 work once
syscall ABI and runtime expectations are clearer.

The destroy path unmaps all leaf mappings before calling `vm_space_destroy()`,
then frees the task-owned backing frames. This preserves the existing VM
invariant that `vm_space_destroy()` only destroys an address space with no leaf
mappings.

Because the kernel is still single-hart, the scheduler's interrupt-masked
critical section is enough to protect this lifecycle transition. SMP would need
a stronger ownership protocol, likely with reference counts or deferred
reclamation.

## Evidence

The `user-task` scenario creates a scheduled U-mode task and a kernel observer
thread. The user task exits through the existing U-mode `exit` syscall. The
observer runs afterward and verifies:

- the user task reached `USER_TASK_DESTROYED`
- the task exit code was recorded
- the free-page count returned to the baseline captured before task creation
- `thread_create_user()` rejects the destroyed task

Expected smoke output includes:

```text
scenario: user-task
user: task lifecycle cleanup passed
milestone 23: user task lifecycle
```

Run:

```sh
make test SCENARIO=user-task
```

## Connections

The ECE350 connection is the split between process lifetime and scheduler
mechanism. The address space is the protection container, but the scheduler is
the component that knows when a thread is no longer runnable or current.

The STM32 RTOS connection is the deferred cleanup pattern around a context
switch. Like an RTOS scheduler, this kernel must avoid mutating scheduler-owned
state while an interrupt/trap path still depends on it. The analogy stops at
virtual memory: freeing page tables and trap-context mappings is specific to
the RISC-V/Sv39 kernel.
