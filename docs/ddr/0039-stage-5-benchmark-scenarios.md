# DDR 39: Stage 5 Benchmark Scenarios

## Status

Accepted

## Context

Stage 5 now has scheduled U-mode tasks, a syscall ABI, C runtime stubs,
task-aware usercopy, a userspace accelerator syscall, negative validation, and
runtime trace events. The next milestone needs performance evidence for those
boundaries.

The benchmark work should improve understanding without weakening the user and
kernel boundary. In particular, userspace should not receive direct cycle
counter access just because measurements are easier that way.

## Decision

Add three benchmark scenarios:

- `benchmark-syscall`
- `benchmark-scheduler`
- `benchmark-accelerator`

Benchmark timestamps are kernel-owned through `csr_read_cycle()`. Benchmark
scenarios default to `CONFIG_TRACE=0`, while still allowing explicit
trace-enabled diagnostic builds.

Add `USER_SYSCALL_NOOP` as a minimal scheduled-user syscall. It returns
`USER_SYSCALL_OK` and lets the kernel accumulate dispatcher-body cycle counts
without adding pointer policy, blocking behavior, or device behavior.

`benchmark-syscall` reports two measurements:

- total cycles for a scheduled U-mode program that calls `user_noop()` in a
  loop and exits
- accumulated noop dispatcher cycles with min/max per-call observations

`benchmark-scheduler` reports cycles for two kernel threads repeatedly yielding
to each other.

`benchmark-accelerator` reports both:

- one full userspace accelerator `memset` path
- a repeated kernel driver-only accelerator baseline

The userspace accelerator benchmark performs one request because the current
userspace ABI does not expose accelerator reset. The driver-only phase resets
between requests because reset is kernel-owned policy.

Smoke validation checks that each benchmark line appears with positive
iteration and cycle fields. It does not enforce timing thresholds.

## Consequences

The benchmark scenarios provide structured evidence for milestone 29 without
making trace output part of correctness. They can run with tracing disabled by
default, so trace instrumentation does not distort the normal measurement path.

`USER_SYSCALL_NOOP` becomes part of the public toy-kernel ABI. That is a small
cost, but it keeps syscall measurement honest: a benchmark-only syscall is less
ambiguous than trying to infer syscall cost from `yield`, which also measures
scheduler behavior.

The syscall benchmark deliberately labels two different costs. The task-level
measurement includes scheduler setup and U-mode loop execution around the noop
calls. The dispatcher measurement excludes trap entry and `sret`. Future work
could add trap-entry timestamps if the kernel needs more precise round-trip
syscall accounting.

Accelerator user-path and driver-only measurements are intentionally separate.
The former measures the isolation-preserving path userspace actually uses. The
latter gives a baseline for the kernel/device mechanism without usercopy and
syscall overhead.

## Alternatives Considered

A single `benchmarks` scenario would reduce Makefile and smoke-test surface
area, but it would mix syscall, scheduler, and accelerator evidence in one long
boot path.

Enabling userspace `rdcycle` would make user-level timing easier, but it would
change counter-access policy and weaken the clean Stage 5 privilege story.

Measuring `yield` as the syscall benchmark would avoid adding `noop`, but it
would conflate syscall dispatch with scheduler behavior.

Using trace records as the benchmark source would reuse PR7 infrastructure, but
trace emission overhead would become part of the measured path.

Adding QEMU timing thresholds now would look like regression coverage, but the
numbers are not stable enough to justify hard pass/fail limits.

## Evidence

Run:

```sh
make test SCENARIO=benchmark-syscall
make test SCENARIO=benchmark-scheduler
make test SCENARIO=benchmark-accelerator
```

Expected output includes benchmark records like:

```text
bench: syscall_noop_task iterations=... cycles_total=... cycles_avg=...
bench: syscall_noop_dispatch iterations=... cycles_total=... cycles_avg=... cycles_min=... cycles_max=...
bench: scheduler_yield_pair iterations=... cycles_total=... cycles_avg=...
bench: accelerator_user_memset iterations=... cycles_total=... cycles_avg=... bytes_total=...
bench: accelerator_driver_memset iterations=... cycles_total=... cycles_avg=... bytes_total=...
milestone 29: performance evaluation
```

## Connections

The ECE350 connection is measurement discipline around mechanism boundaries.
Like timing a context switch or blocking primitive in an RTOS lab, the useful
question is not just "how many cycles?" but "which boundary did that number
actually include?"

The STM32 RTOS analogy is scheduler and peripheral timing: one benchmark
isolates cooperative yield/context-switch behavior, while the accelerator
benchmark separates the task-facing API from the driver/device path. The analogy
breaks at user/kernel isolation because this RISC-V kernel must preserve
usercopy and page-table boundaries while measuring the path.
