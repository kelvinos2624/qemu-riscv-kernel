# DDR 4: Save a Full Trap Frame

## Status

Accepted

## Context

The first trap handler only needs a few machine CSRs, but later milestones need
trap state for syscalls, preemption, context switching, and debugging.

## Decision

Save all general-purpose registers except `x0`, plus `mepc`, `mstatus`,
`mcause`, and `mtval`, on every trap entry.

## Alternatives Considered

- Save only caller-saved registers.
- Save only the registers used by the current C trap handler.
- Use separate minimal frames for exceptions and interrupts.

## Consequences

A full frame costs more memory stores on each trap, but it provides a stable
debugging and scheduling foundation. The cost is acceptable before there are
measured trap-latency constraints. Compile-time C offset checks guard the
assembly/C layout contract.

## Evidence

`kernel/arch/riscv64/trap.S` defines the saved layout. `kernel/core/trap.h`
defines the matching C structure. `kernel/core/trap.c` checks critical offsets
with `_Static_assert`.
