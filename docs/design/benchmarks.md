# Benchmark Design

## Scope

Stage 5 benchmark scenarios provide structured cycle-count evidence for the
main runtime boundaries introduced by the userspace work:

- scheduled U-mode syscall dispatch
- kernel scheduler yield/context-switch behavior
- userspace accelerator syscall path
- kernel driver-only accelerator path

The scenarios are smoke-testable evidence, not hardware performance claims.
QEMU cycle counts are useful for comparing relative paths in this kernel, but
they are not stable enough for regression thresholds yet.

## Counter Ownership

Benchmark timestamps are kernel-owned. S-mode reads the RISC-V cycle counter
through `csr_read_cycle()`, enabled by the M-mode shim through `mcounteren.CY`.
Userspace does not receive direct `rdcycle` access.

This preserves the Stage 5 isolation shape: user programs can request work, but
they do not gain new timing authority just because benchmark scenarios need
measurements.

## Output Format

Benchmark lines use stable key/value text:

```text
bench: <name> iterations=0x... cycles_total=0x... cycles_avg=0x...
```

Accelerator benchmarks also print `bytes_total`. The noop dispatcher benchmark
prints `cycles_min` and `cycles_max` because its measurements are accumulated
inside the syscall dispatcher one call at a time.

Smoke tests require the expected benchmark names and positive cycle/iteration
fields. They intentionally do not enforce timing thresholds.

## Current Benchmarks

`benchmark-syscall` runs a C userspace program that calls `user_noop()` in a
loop and exits. The scenario reports:

- `syscall_noop_task`: total cycles from before scheduler start until the
  scheduled user task exits
- `syscall_noop_dispatch`: accumulated cycles inside the noop syscall
  dispatcher body

The first number includes scheduler/task setup noise around the U-mode loop.
The second number excludes trap entry and return. Keeping both labels explicit
prevents the benchmark from pretending to measure a single universal syscall
cost.

`benchmark-scheduler` runs two kernel threads that repeatedly yield to each
other. It reports `scheduler_yield_pair`, whose iteration count is the total
number of yields across both threads.

`benchmark-accelerator` runs in two phases:

- `accelerator_user_memset`: one full userspace accelerator syscall request,
  including user pointer validation, bounce-buffer work, driver submission,
  simulated completion, and copyback
- `accelerator_driver_memset`: repeated kernel driver-only descriptor
  submissions with kernel-owned reset between requests

The userspace phase performs one request because the current public userspace
accelerator ABI does not expose reset. The driver-only phase can reset between
requests because reset remains kernel policy.

## Invariants

Benchmarks must not:

- expose `rdcycle` directly to userspace
- bypass usercopy validation
- hand user physical pages to the simulated device
- depend on trace records for correctness
- fail solely because QEMU cycle counts changed

Trace-enabled builds still work, but benchmark scenarios default to
`CONFIG_TRACE=0` so the normal smoke path measures without trace instrumentation.
