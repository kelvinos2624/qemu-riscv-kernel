# DDR 37: Syscall Negative Validation

## Status

Accepted

## Context

Stage 5 now has scheduled U-mode tasks, runtime syscall stubs, task-aware
usercopy, and a userspace accelerator `MEMSET` syscall. The happy paths prove
that userspace can request scheduler and accelerator work. The next boundary
needs to prove that bad userspace requests stay contained.

PR3 intentionally panicked on unknown syscall numbers because the kernel did not
yet have a runtime, negative tests, or a generic syscall error convention. PR6
is the point where unsupported syscall numbers become part of the user-visible
ABI instead of a kernel crash.

## Decision

Add `USER_SYSCALL_ERR_UNKNOWN` to the generic syscall ABI and return it for
unsupported syscall numbers from scheduled user tasks. The dispatcher advances
the saved user PC before returning so the caller does not repeat the same
`ecall`.

Keep raw syscall access private to the negative test program. Public userspace
headers still expose named runtime calls rather than generic `syscallN`
helpers.

Add one `syscall-negative` C user program that verifies:

- unknown syscall number returns `USER_SYSCALL_ERR_UNKNOWN`
- null accelerator destination returns `USER_ACCEL_ERR_INVALID`
- zero length returns `USER_ACCEL_ERR_INVALID`
- length greater than `USER_ACCEL_MEMSET_MAX_LEN` returns invalid
- mapped-looking but unmapped destination returns invalid
- read-only code-page destination returns invalid
- wrapping address range returns invalid
- timed-out request returns `USER_ACCEL_ERR_TIMEOUT` without modifying the user
  buffer
- a valid request after timeout still succeeds

The scenario observer delays simulated hardware stepping long enough for the
timeout subcase, then steps the accelerator and dispatches IRQs until the user
task exits.

Harden the M-mode timer-shim trap entry with an `mscratch` emergency stack. The
negative scenario's dense syscall loop exposed the old limitation documented in
DDR 21: a machine timer interrupt could save its frame using the interrupted
stack pointer. That is not valid once execution can be near a U-mode return
boundary.

## Consequences

Unknown syscall numbers from scheduled userspace are now user errors, not kernel
invariant failures. The dispatcher still panics for kernel-path misuse such as
a null syscall frame or scheduler-only syscall handling without a current user
task.

Accelerator argument validation remains accelerator-specific. Bad accelerator
pointers and lengths return `USER_ACCEL_ERR_INVALID`, not a generic syscall
error. This keeps the syscall-number mechanism separate from operation-specific
policy.

The test intentionally exercises invalid pointer classes through the public
accelerator runtime API rather than exposing raw usercopy helpers to userspace.
That proves the syscall boundary visible to a real user task.

M-mode now has its own trap-save stack. This preserves the privilege-boundary
invariant independently of S-mode's `sscratch` convention and keeps the
machine-timer shim policy-free.

## Alternatives Considered

Keeping unknown syscalls as panic would preserve the PR3 bring-up behavior, but
it would make unsupported syscall numbers a kernel-fatal condition even after
the ABI has a testable userspace runtime.

Adding public raw `user_syscallN()` helpers would make negative tests shorter,
but it would expose a low-level ABI surface before there is a stable errno model
or libc-like wrapper convention.

Splitting kernel-side usercopy negative tests from user-visible syscall tests
would isolate helper branches more precisely, but the Stage 5 risk is currently
at the syscall boundary: does a real scheduled U-mode caller receive controlled
results for bad requests?

## Evidence

Expected smoke output includes:

```text
scenario: syscall-negative
user: entering u-mode pc=... sp=... satp=...
user: unknown syscall
user: accel memset invalid
user: accel memset timeout
user: accel memset
user: exited code=...
milestone 22: user address-space switching
user: syscall validation passed
milestone 27: syscall validation
```

Run:

```sh
make test SCENARIO=syscall-negative
```

## Connections

The ECE350 connection is the difference between a user error and a kernel
invariant violation. A valid trap with an unsupported syscall number is a bad
request from userspace; it should return an error through the ABI. A null trap
frame or impossible scheduler state is still a kernel bug.

The STM32 RTOS analogy is parameter validation at an API boundary before a task
is allowed to affect shared kernel or device state. The analogy breaks at the
RISC-V address-space boundary: here, invalid pointers are architectural virtual
addresses that must be checked against the task page table before any copy or
device submission.
