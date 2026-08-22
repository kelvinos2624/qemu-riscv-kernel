# DDR 3: Use Direct Trap Vector Mode First

## Status

Accepted

## Context

The kernel needs trap handling before timer interrupts, preemption, syscalls,
page faults, or external device interrupts can be implemented. At this stage,
debuggability is more important than interrupt dispatch speed.

## Decision

Install `mtvec` in direct mode and route all traps through one assembly entry,
`trap_entry`, then into the C handler `trap_handle`.

## Alternatives Considered

- Use vectored `mtvec` mode immediately.
- Use separate hand-written assembly paths for each expected trap cause.
- Delay trap handling until scheduler work begins.

## Consequences

Direct mode makes the first trap path easier to reason about and test. The
tradeoff is that interrupts require cause decoding in C rather than jumping
directly to per-cause vectors. If measured interrupt latency later requires it,
the kernel can move to vectored mode behind the same `trap_init` boundary.

## Evidence

The path is implemented in `kernel/arch/riscv64/trap.S` and
`kernel/core/trap.c`. The boot test now waits for the milestone 2 trap-vector
banner, which is printed only after a controlled `ecall` trap returns.
