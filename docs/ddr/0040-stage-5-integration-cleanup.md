# DDR 40: Stage 5 Integration Cleanup

## Status

Accepted

## Context

Stage 5 added scheduled U-mode execution under separate user page tables,
task lifetime, a syscall ABI, freestanding C runtime stubs, a userspace
accelerator API, negative validation, runtime tracing, and benchmark scenarios.
Each feature PR added focused smoke evidence, but the end of the stage needs a
single host-side way to prove the Stage 5 surface.

The cleanup PR should close the stage without adding a new kernel subsystem.
The existing scenarios already cover the important behavior and failure cases.

## Decision

Add a host-side Stage 5 smoke grouping:

```sh
make test-stage5
```

The target runs the Stage 5 scenarios:

```text
user-satp
user-task
syscall-basic
user-runtime
user-accelerator
syscall-negative
runtime-tracing
benchmark-syscall
benchmark-scheduler
benchmark-accelerator
```

Mark Stage 5 complete in the README and record PR9 evidence as the grouped
Stage 5 smoke target.

## Consequences

The integration proof stays host-side. The kernel keeps individual scenarios as
the mechanism for focused evidence, while the Makefile owns the policy for which
scenarios constitute Stage 5 completion.

The benchmark scenarios are included because milestone 29 is part of Stage 5.
They keep their existing default of `CONFIG_TRACE=0`, so the grouped target
does not accidentally turn tracing overhead into benchmark evidence.

There is no new `stage5-integration` kernel scenario. That avoids duplicating
scenario logic and keeps the cleanup PR focused on evidence and documentation.

## Alternatives Considered

A docs-only cleanup would mark the stage complete with the least code churn, but
it would not give a direct command equivalent to Stage 4's `make test-stage4`.

A single kernel integration scenario would provide one smoke marker, but it
would mix multiple boundaries into a long boot path and duplicate existing
scenario checks.

Refactoring `kernel/core/scenario.c` into per-stage files remains possible, but
that is a maintainability change rather than Stage 5 completion evidence.

## Evidence

PR9 evidence is:

```sh
make test-stage5
make test
```

The Stage 5 grouped smoke target observes milestones 22 through 29.

## Connections

The ECE350 connection is test grouping as policy over existing mechanisms. Each
scenario is like an individual kernel invariant test, while `make test-stage5`
is the lab-checklist command that says which invariants prove the milestone.
