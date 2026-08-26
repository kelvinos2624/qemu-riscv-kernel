# Memory Management Design

## Scope

This milestone starts the virtual-memory and allocation section with a physical
page allocator. The allocator owns free RAM after the kernel image and hands out
fixed-size page frames for later kernel heap, page-table, user-space, and driver
work.

This is physical allocation only. The kernel still runs with address
translation disabled, so returned page addresses are currently identity-mapped
machine addresses that C code can use directly as pointers. Later virtual-memory
work may wrap these values in a physical-address type once virtual and physical
addresses are no longer interchangeable.

## RAM Bounds

The linker script defines the current QEMU `virt` RAM contract:

- `__ram_start`: start of DRAM at `0x80000000`
- `__ram_end`: end of the configured 128 MiB RAM window
- `__kernel_end`: end of kernel-owned image, BSS, allocator metadata, and boot
  stack

`page_init()` receives `__kernel_end` and `__ram_end`, aligns the range to 4 KiB
boundaries, and treats only that aligned range as allocatable.

The linker symbol is a build-time platform contract, not runtime memory
discovery. If QEMU RAM size becomes variable, a device-tree parser or platform
description should provide the RAM ranges and reserved regions.

## Page Granularity

The allocator uses 4 KiB pages. This matches the RISC-V Sv39 base page size and
the existing linker section alignment, so frames allocated in this milestone can
later back page-table pages, user mappings, kernel stacks, heap spans, and driver
buffers without changing granularity.

The ECE350 virtual-memory notes frame paging as fixed-size physical allocation
with a page offset copied unchanged through translation. A 4 KiB page keeps that
model concrete while avoiding the larger internal fragmentation that would come
from coarse segment-sized allocation.

## Bitmap Allocator

The allocator uses a bitmap:

- one bit represents one managed physical page
- `0` means free
- `1` means allocated

The bitmap itself is linker-reserved kernel memory between
`__page_bitmap_start` and `__page_bitmap_end`, so allocator metadata is not
stored inside pages that may later be returned to callers.

Allocation scans from a saved hint and wraps around. This preserves simple
bitmap semantics while keeping full exhaustion tests practical. Freeing a page
clears its bit and may move the allocation hint backward so recently freed pages
can be reused promptly.

## Safety Rules

Allocator metadata updates are protected with `irq_save()` and `irq_restore()`.
This is enough for the current single-hart preemptive kernel and matches the
same short critical-section pattern used by the scheduler and synchronization
subsystems.

`page_free()` panics on:

- unaligned addresses
- addresses outside the managed range
- double frees

Returning `NULL` from `page_alloc()` is the normal exhaustion behavior.

## STM32 RTOS Connection

The STM32 RTOS lab path favors bounded kernel-owned structures and predictable
failure over unbounded host-style allocation. This allocator follows that same
discipline: fixed-size frames, explicit ownership, deterministic exhaustion, and
short interrupt-masked critical sections.

The next layer, a kernel heap, can subdivide these frames for smaller kernel
objects. Small object allocation is intentionally not part of this milestone.

## Test Evidence

The boot self-test verifies:

- the initialized free count is nonzero
- allocated pages are aligned
- two live allocations are distinct
- a freed page is reused
- allocating all pages eventually returns `NULL`
- freeing all exhausted pages restores the original free count

The QEMU smoke test checks for:

```text
milestone 11: physical page allocator
```
