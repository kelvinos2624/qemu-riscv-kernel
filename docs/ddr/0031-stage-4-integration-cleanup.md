# DDR 31: Stage 4 Integration Cleanup

## Status

Accepted

## Context

Stage 4 introduced the kernel's first real driver framework and a simulated
accelerator in five incremental PRs. Each PR added focused evidence, but the
end of the stage needs a coherent scenario naming scheme, a direct way to run
only Stage 4 evidence, and top-level docs that describe Stage 4 as complete.

The existing kernel scenarios already prove the important behavior. The cleanup
PR should preserve those behaviors rather than adding a new kernel capability.

## Decision

Normalize accelerator scenario names to use the `accelerator-*` prefix. The
register-model scenario is renamed from `accel-registers` to
`accelerator-registers`, matching the descriptor, IRQ-completion, and
timeout/error scenarios.

Refactor scenario selection in the Makefile from one nested conditional
expression into named `SCENARIO_ID_*` variables. This keeps the mapping between
host scenario names and kernel `CONFIG_SCENARIO` IDs explicit and easier to
extend.

Add a host-side Stage 4 smoke grouping:

```sh
make test-stage4
```

The target runs the five Stage 4 scenarios:

```text
driver-framework
accelerator-registers
accelerator-descriptors
accelerator-irq-completion
accelerator-timeout-error-handling
```

Mark Stage 4 complete in the README and record PR6 evidence as the grouped
Stage 4 smoke target.

## Consequences

The integration proof stays host-side. There is no new kernel scenario or fake
milestone 22 because the existing milestones 17-21 already cover the Stage 4
behavioral surface.

The scenario rename intentionally breaks the old `accel-registers` command in
favor of a consistent public scenario namespace. This is acceptable at the
stage boundary because docs, smoke tests, and the Makefile now agree on the new
name.

The Makefile still owns the host-name to `CONFIG_SCENARIO` policy. The kernel
continues to receive only a numeric scenario ID, so runtime behavior is
unchanged.

## Alternatives Considered

Keeping the old `accel-registers` alias was rejected because the stage cleanup
is meant to leave one clear public name per scenario.

Adding a `stage4-integration` kernel scenario was rejected because it would
duplicate existing scenario evidence and risk turning integration cleanup into
new kernel behavior.

Splitting `kernel/core/scenario.c` into per-stage files was deferred. That may
become worthwhile if scenario code keeps growing, but this PR can remove the
most painful scenario-selection clutter without widening the blast radius.

## Evidence

PR6 evidence is:

```sh
make test-stage4
make test
```

The Stage 4 grouped smoke target observes milestones 17 through 21.

## Connections

The ECE350 connection is test grouping as policy rather than mechanism. The
kernel scenarios remain individual mechanisms for proving behavior, while the
host Makefile decides which subset of scenarios represents Stage 4 completion.
