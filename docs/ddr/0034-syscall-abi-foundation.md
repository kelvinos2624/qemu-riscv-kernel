# DDR 34: Syscall ABI Foundation

## Status

Accepted

## Context

The kernel now has two U-mode paths:

- the legacy `first-user` scenario, which enters U-mode without the scheduler
- scheduled user tasks, which run under a separate user `satp` through the
  Stage 5 trampoline

Both paths reach S-mode through a delegated U-mode `ecall`. Before this change,
the trap handler special-cased user exit directly. That was enough to prove the
first privilege boundary, but it mixed trap classification with syscall policy
and left no natural home for additional user requests.

The next Stage 5 work needs a syscall boundary, but not yet a full userspace
runtime, pointer validation policy, or accelerator-facing ABI.

## Decision

Introduce a dispatcher-owned user syscall ABI:

```text
a7 = syscall number
a0 = first argument
a0 = return value when the syscall returns to U-mode
```

Initial syscall numbers:

```text
USER_SYSCALL_EXIT  = 1
USER_SYSCALL_YIELD = 2
USER_SYSCALL_SLEEP = 3
```

`trap.c` owns only trap classification. When it observes a U-mode `ecall`, it
calls `user_syscall_dispatch(frame)`.

`user/syscall.c` owns syscall policy:

- dispatch by syscall number
- advance `sepc` for syscalls that return
- set return values
- report user exits
- preserve the legacy `first-user` continuation
- retire scheduled user tasks through the thread layer
- panic on unknown syscall numbers

`thread.c` owns scheduler mechanisms. It exposes trap-frame helpers that let
callers yield or sleep the current thread from a trap-return path. Those helpers
do not know syscall numbers or ABI registers.

## Consequences

`yield()` and `sleep(ticks)` are available only for scheduled user tasks. Calling
them from the legacy unscheduled `first-user` path panics because there is no
current scheduler-owned user task to yield or block.

`exit(code)` still supports both worlds. Scheduled user tasks retire through the
task lifecycle path and eventually destroy task-owned pages after switching away
from the exiting context. The legacy `first-user` scenario still rewrites the
saved frame to return to its S-mode continuation.

Unknown syscalls panic for now. That is intentionally strict for the foundation
PR: it exposes accidental ABI mismatches immediately instead of inventing an
error-number contract before the runtime and negative tests exist.

This PR avoids pointer-bearing syscalls. That defers the harder usercopy policy:
which layer validates user buffers, when `SUM` is enabled, and whether payloads
are copied or pinned for device work.

## Alternatives Considered

Keeping syscall handling in `trap.c` would be smaller, but it would make the
trap layer own ABI policy. That does not scale once syscalls need validation,
copying, blocking, and error conventions.

Returning an error for unknown syscalls would be friendlier to a mature
userspace runtime, but the kernel does not yet have an errno convention or
negative syscall tests. Panic gives a crisp smoke-test failure during ABI
bring-up.

Adding pointer-bearing syscalls now would connect too many policies at once:
register ABI, usercopy, memory ownership, and accelerator command validation.
The first ABI slice stays integer-only so the scheduler interaction can be
proved independently.

## Evidence

The `syscall-basic` scenario runs a scheduled user program that calls:

```text
yield()
sleep(2)
exit(0)
```

Expected smoke output includes:

```text
scenario: syscall-basic
user: syscall yield
user: syscall sleep ticks=...
user: exited code=...
milestone 24: syscall ABI
```

Run:

```sh
make test SCENARIO=syscall-basic
```

## Connections

The ECE350 connection is the split between trap mechanism and syscall policy.
Hardware moves control from U-mode to S-mode, but the kernel-owned dispatcher
interprets the saved register contract.

The STM32 RTOS connection is weaker than the timer/PendSV analogy, but the
scheduler part is familiar: `yield` and `sleep` are controlled scheduling
points that reuse the same trap-return context-switch machinery as kernel
threads. The RISC-V-specific part is the privilege and address-space transition
around that scheduling decision.
