# DDR 17: Use a Bitmap Physical Page Allocator

## Status

Accepted

## Context

The kernel is entering the virtual-memory and allocation section. Later work
needs page-sized physical frames for page tables, user mappings, kernel heap
spans, dynamic stacks, and driver buffers.

The current kernel runs on QEMU `virt` with address translation disabled. The
linker script already controls the kernel layout and now exports RAM-bound
symbols for the current 128 MiB platform contract.

## Decision

Add a 4 KiB bitmap physical page allocator.

The linker script exports:

- `__ram_start`
- `__ram_end`
- `__kernel_end`
- `__page_bitmap_start`
- `__page_bitmap_end`

The allocator initializes the managed range from
`align_up(__kernel_end, 4096)` to `align_down(__ram_end, 4096)`. It uses one
bitmap bit per managed page, where `0` means free and `1` means allocated.

The public API is:

```c
void page_init(uintptr_t mem_start, uintptr_t mem_end);
void *page_alloc(void);
void page_free(void *page);
size_t page_free_count(void);
size_t page_total_count(void);
uintptr_t page_managed_start(void);
uintptr_t page_managed_end(void);
```

Allocator metadata updates are protected with `irq_save()` and `irq_restore()`.

## Rationale

4 KiB pages match the RISC-V Sv39 base page size and the kernel's existing
linker section alignment. That lets this allocator feed future page-table and
userspace work without changing its frame size.

A bitmap is compact and easy to inspect. It also gives direct invalid-free and
double-free checks, which are useful for allocator invariant tests. With the
current 128 MiB QEMU RAM configuration, the full physical memory bitmap is small
enough to reserve in the kernel image.

The linker-provided `__ram_end` avoids scattering raw RAM-end constants in C.
It is still a build-time platform contract rather than runtime discovery. A
future platform layer or device-tree parser should provide RAM ranges if the
kernel supports variable machine configurations or reserved regions.

## Alternatives Considered

### Free List

A free list provides fast allocation and can store its links inside free pages.
It was not chosen first because debug checks are less direct and allocator
metadata would live in memory that later callers receive.

### Buddy Allocator

A buddy allocator can support larger contiguous allocations and coalescing. It
is more complex than this milestone needs. If future driver or DMA work needs
larger contiguous runs, buddy allocation can replace or sit above the bitmap
policy.

### Early Bump Allocator Only

A bump allocator is simple but cannot free pages. It would not prove ownership,
reuse, exhaustion, or double-free invariants, so it is too weak for this
milestone.

## Consequences

`page_alloc()` returns identity-addressed physical pages for now because paging
is disabled. This must be revisited when virtual memory is enabled.

The bitmap allocation path is O(n) in the worst case, but the allocator keeps a
next-free hint so ordinary allocation and full exhaustion tests avoid repeatedly
starting at page zero.

The allocator does not zero pages. Callers that need zero-filled memory, such as
future page-table creation, should either clear the page after allocation or use
a later zeroing helper.

## ECE350 and STM32 RTOS Connection

This mirrors the ECE350 paging model: physical memory is divided into fixed-size
pages and free frames can be tracked with a bitmap. It also follows the STM32
RTOS lab style of bounded kernel-owned metadata and short interrupt-masked
critical sections, rather than relying on an unbounded general-purpose heap.

## Evidence

The boot self-test allocates distinct aligned pages, checks free-count
accounting, verifies immediate reuse after free, exhausts all managed pages
until `page_alloc()` returns `NULL`, then frees them and verifies the initial
free count is restored.

The QEMU smoke test checks for:

```text
milestone 11: physical page allocator
```
