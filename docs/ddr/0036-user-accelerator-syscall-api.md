# DDR 36: Userspace Accelerator Syscall API

## Status

Accepted

## Context

Stage 4 created a kernel-only simulated accelerator driver. Kernel threads can
submit page-backed command descriptors, block for interrupt-driven completion,
and use timeout-aware recovery.

Stage 5 now has scheduled U-mode tasks, a per-task user page table, a syscall
dispatcher, and text-only C runtime stubs. The next step is to let C userspace
request accelerator work without weakening the isolation shape established by
the user-satp trampoline.

The important invariant is:

```text
The device may operate only on kernel-owned physical memory, even when the
request originated in userspace.
```

## Decision

Add a scalar userspace accelerator syscall:

```text
a7 = USER_SYSCALL_ACCEL_MEMSET
a0 = user destination
a1 = byte length
a2 = fill value
a3 = timeout ticks
a0 = status return
```

The user runtime exposes this as:

```c
int user_accel_memset(
    void *dst,
    uint32_t len,
    uint32_t value,
    uint64_t timeout_ticks);
```

The syscall implementation lives in `kernel/user/accel_syscall.c`. It validates
the destination against the scheduled task's address space, allocates a
page-backed kernel bounce buffer, submits accelerator `MEMSET` against that
kernel buffer, and copies the completed bytes back to the task with
`copy_to_user_task()`.

If a nonzero-timeout submission returns `USER_ACCEL_ERR_TIMEOUT`, the syscall
resets the accelerator before freeing the temporary descriptor and bounce page.
The reset proves that the simulated device has forgotten the command base.

The first transfer limit is `0 < len <= PAGE_SIZE`. The user range may cross
user pages because task-aware usercopy walks the task page table page by page.
The device-facing bounce buffer remains page-contained to satisfy the existing
accelerator descriptor contract.

`include/user_abi.h` owns syscall numbers. `include/user_accel.h` owns
user-visible accelerator statuses, limits, and runtime prototypes.

## Consequences

The syscall boundary owns validation and copy policy. The accelerator driver
remains a kernel mechanism that consumes descriptors over kernel-owned physical
memory; it does not learn about user tasks, user virtual addresses, or syscall
ABI registers.

The task-aware usercopy helpers validate against a ready `user_task_t` rather
than the kernel page table. They then copy through the physical frame returned
by `vm_get_mapping()`, relying on the current identity-mapped kernel RAM window
instead of temporarily enabling `SSTATUS_SUM`.

Blocking inside this syscall exposed a trap-return invariant: a user syscall may
sleep on a kernel continuation frame, but when it resumes and returns the
original user trap frame, the trap return path must still use the task-owned
user-satp trampoline.

Timeout also exposes a memory-ownership invariant: a descriptor page and bounce
page cannot return to the allocator while the device can still observe `CMD_BASE`
or `dst_pa`. The syscall owns those temporary pages, so it also owns the reset
needed to quiesce the device before freeing them after timeout.

Userspace now has an accelerator-specific public header. That keeps the generic
syscall-number header small, but it means status codes and limits are part of a
real user ABI and need compatibility discipline in later PRs.

## Alternatives Considered

Passing user pages directly to the device would avoid a copy, but it would mix
user memory ownership with device ownership before the kernel has page pinning,
frame ownership metadata, or an IOMMU-like policy.

Accepting a userspace descriptor would be closer to a richer accelerator API,
but it would force descriptor validation, payload pin/copy policy, and ABI
versioning before the runtime has broad negative syscall tests.

Limiting the destination to one user page would simplify validation, but it
would make usercopy less reusable and would expose an arbitrary ABI restriction
that the current task-aware copy path does not require.

Hiding timeout policy in the kernel would make the first runtime call smaller,
but it would also remove an important part of the existing Stage 4 driver
contract from userspace.

## Evidence

The `user-accelerator` scenario builds a text-only C user program that calls
`user_accel_memset()` on a stack buffer, verifies every byte was set, returns
from `user_main()`, and exits through the runtime.

Expected smoke output includes:

```text
scenario: user-accelerator
user: entering u-mode pc=... sp=... satp=...
user: accel memset timeout
user: accel memset
user: exited code=...
milestone 22: user address-space switching
user: accelerator memset passed
milestone 26: user accelerator API
```

Run:

```sh
make test SCENARIO=user-accelerator
```

## Connections

The ECE350 connection is the split between mechanism and policy. The accelerator
driver is the mechanism for descriptor submission and completion; the syscall
layer owns user pointer validation, copy policy, and ABI return values.

The STM32 RTOS analogy is a task asking a driver to perform peripheral work and
blocking until an ISR completes it. The analogy breaks at memory isolation:
this kernel must translate and validate a U-mode pointer before involving the
device, while the STM32 project did not have a hardware-enforced user/kernel
address-space split.
