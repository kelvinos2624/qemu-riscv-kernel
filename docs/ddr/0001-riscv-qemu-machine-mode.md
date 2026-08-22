# DDR 1: Use RISC-V QEMU Machine Mode for Initial Boot

## Status

Accepted

## Context

The first milestone needs to prove that the kernel image, linker script, stack
setup, and console output all work before introducing firmware interfaces,
supervisor mode, virtual memory, or syscalls.

## Decision

Run on QEMU `virt` as a RISC-V 64-bit freestanding kernel in machine mode using
`-bios none`, linked at `0x80000000`.

## Alternatives Considered

- Start under OpenSBI in supervisor mode.
- Start with a host process simulation before using QEMU.
- Target physical hardware first.

## Consequences

Machine mode keeps the first boot path small and directly controlled by our own
assembly. It also means supervisor-mode behavior, SBI calls, and privilege
transitions are deferred to a later milestone and must be documented when added.

## Evidence

The boot path is defined by `kernel/arch/riscv64/boot.S`, `linker.ld`, and the
`make run` QEMU command.
