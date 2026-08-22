# DDR 5: Use QEMU Machine Timer Directly

## Status

Accepted

## Context

The kernel needs a periodic interrupt source before it can implement sleep,
preemption, or scheduler time accounting. There is no device-tree parser or
general platform bus yet.

## Decision

Use the QEMU `virt` machine timer through fixed CLINT/ACLINT-compatible MMIO
addresses and handle machine-timer interrupts in the common trap path.

## Alternatives Considered

- Parse the flattened device tree to discover timer addresses.
- Depend on OpenSBI timer calls and move to supervisor mode now.
- Delay timer work until a broader device framework exists.

## Consequences

Fixed MMIO addresses keep the milestone small and testable. The tradeoff is
that the timer driver is platform-specific. A later platform layer or
device-tree parser should own these constants if the kernel grows beyond QEMU
`virt`.

## Evidence

`kernel/drivers/timer.c` programs `mtimecmp`, enables `mie.MTIE` and
`mstatus.MIE`, and updates a monotonic tick counter. The boot test waits for
the timer milestone banner, which is printed only after multiple timer
interrupts have been observed.
