# DDR 18: Use Selectable Kernel Scenarios for QEMU Evidence

## Status

Accepted

## Context

The kernel now has several milestone demonstrations: boot, traps, timer,
physical page allocation, scheduler preemption, wait queues, mutexes, timed
waits, and tracing. Keeping every demonstration in `kmain()` would make the boot
path a growing script of unrelated subsystem checks.

The next Stage 3 milestones need focused evidence for heap allocation,
page-table mapping, page faults, user-mode entry, usercopy validation, and
driver completion. Those tests should share platform bring-up without all
running in one monolithic workload.

## Decision

Split common kernel bring-up from selectable scenario workloads.

`kmain()` remains responsible for:

- boot banner
- trap setup and self-test
- timer setup
- physical page allocator initialization
- scenario dispatch

Scenario code owns focused milestone workloads:

- `allocator`
- `scheduler-sync`

The Makefile selects scenarios at compile time with `SCENARIO=<name>`, maps
names to numeric `CONFIG_SCENARIO` values, and keeps each scenario in a separate
build directory. `make test` runs all current scenarios. `make test
SCENARIO=<name>` runs one.

## Rationale

Compile-time selection avoids boot-argument plumbing while the kernel still has
no command-line parser, device-tree parser, filesystem, or userspace runtime.
Separate build directories avoid stale object files when `CONFIG_SCENARIO`
changes.

The term "scenario" is intentional. These workloads are QEMU evidence and
human-readable demos, not a full in-kernel unit-test framework.

## ECE350 and STM32 RTOS Connection

This mirrors the ECE350/STM32 RTOS lab pattern: one reusable kernel substrate
runs different lab applications that create tasks and exercise one behavior at a
time. The scheduler, memory, and synchronization subsystems should not depend on
one giant application script to prove their behavior.

## Consequences

Adding future Stage 3 tests should be a matter of adding a scenario and a smoke
test expectation, not extending a single global boot transcript.

Each scenario requires a rebuild. That is acceptable for now and keeps the
runtime kernel simple.

## Evidence

`make test` currently runs:

```text
allocator
scheduler-sync
```

Each scenario is checked through stable UART markers.
