# Trap Design

## Scope

Milestone 2 installs a machine-mode trap vector, saves a full general-purpose
register frame, enters a C trap handler, and proves the path with a controlled
machine-mode `ecall` self-test.

## Trap Mode

The kernel uses direct `mtvec` mode. All exceptions and interrupts enter the
same assembly routine, `trap_entry`.

Direct mode is intentionally simple: it gives one path to debug before the
kernel has many interrupt sources. Vectored mode can be introduced later if
timer or external interrupt latency becomes important enough to measure.

`trap_entry` is explicitly 4-byte aligned because `mtvec` stores the trap mode
in the low two bits of the CSR value.

## Trap Frame

`trap_entry` saves all general-purpose registers except `x0`, plus `mepc`,
`mstatus`, `mcause`, and `mtval`. It then passes a `trap_frame_t *` to
`trap_handle`.

The full frame costs more stores than a minimal exception-only frame, but it is
a better foundation for later syscalls, context switching, and diagnostics.
Compile-time checks in `trap.c` verify that the C structure offsets match the
assembly layout.

## Return Path

After `trap_handle` returns, the assembly entry restores `mepc`, `mstatus`, and
the saved general-purpose registers, then executes `mret`.

The self-test handles a deliberate machine-mode `ecall`, advances `mepc` by 4,
and returns to the instruction after the `ecall`. Unexpected traps print CSR
diagnostics and panic.

## Next Work

- Add interrupt enable/disable helpers.
- Replace the `ecall` self-test with syscall tests once userspace exists.
