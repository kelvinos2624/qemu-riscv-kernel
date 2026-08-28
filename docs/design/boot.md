# Boot Design

## Scope

Milestone 1 boots a single-hart RISC-V kernel under QEMU `virt`, initializes the
kernel stack, clears `.bss`, and writes a banner to the UART.

## Entry Path

QEMU starts the kernel at `_start` with `-bios none`. The kernel is linked at
`0x80000000`, the DRAM base used by the QEMU `virt` machine. `_start` is written
in assembly because C code requires a valid stack and zeroed global state before
it can run predictably.

The earliest boot path still runs in M-mode. After C starts, `kmain()` initializes
the physical page allocator, builds the identity-mapped Sv39 kernel page table,
installs the M-mode machine shim, writes `satp`, executes `sfence.vma`, and
enters `supervisor_main()` with `mret`. Normal kernel execution after that point
runs in S-mode.

## Stack

The linker script reserves a 16 KiB boot stack after `.bss` and exports
`__stack_top`. The boot code loads this symbol into `sp` before calling `kmain`.

## BSS

The boot code clears memory from `__bss_start` to `__bss_end` using 64-bit
stores. This gives C global and static zero-initialized objects their required
initial state without depending on a runtime.

## Console

Early console output uses the QEMU `virt` 16550 UART at `0x10000000`. This is a
platform constant for now; a later driver layer can replace it with device-tree
or static device-table discovery.

The UART page is identity-mapped before S-mode entry so the same console driver
works before and after paging is enabled.

## Next Work

- Extend boot integration coverage beyond the milestone banner.
