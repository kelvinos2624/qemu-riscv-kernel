# Memory Management Design

## Scope

This design note covers the memory-management work that starts the
virtual-memory and allocation section. The physical page allocator owns free RAM
after the kernel image and hands out fixed-size page frames. The kernel heap
then lazily subdivides some of those frames into smaller size-class pools for
kernel objects.

The kernel still runs with address translation disabled, so returned page and
heap addresses are currently identity-mapped machine addresses that C code can
use directly as pointers. Later virtual-memory work may wrap physical-frame
values in a physical-address type once virtual and physical addresses are no
longer interchangeable.

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

## Kernel Heap

The kernel heap exposes:

```c
void heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *ptr);
size_t heap_page_count(void);
size_t heap_free_bytes(void);
size_t heap_allocated_bytes(void);
```

The heap is implemented as a page-backed size-class pool allocator. Size classes
are:

```text
32, 64, 128, 256, 512, 1024, 2048
```

`kmalloc(size)` rounds up to the smallest supported class. Requests of size zero
or greater than 2048 bytes return `NULL`. Larger allocations should use whole
pages directly or wait for a future multi-page heap policy.

`heap_init()` is lazy. It initializes pool metadata but does not allocate heap
pages. When a size class has no free block available, the heap calls
`page_alloc()`, initializes the returned 4 KiB page as a pool page for that one
class, and returns one block from it.

## Pool Pages

Every heap pool page is one 4 KiB physical page. A pool page contains:

- a page header
- a per-page allocation bitmap
- a free-list head
- fixed-size blocks for one size class

All blocks inside a pool page have the same size. Different pool pages can serve
different size classes.

`kfree(ptr)` recovers the owning pool page with:

```c
page = align_down(ptr, PAGE_SIZE);
```

The page header provides the block size and allocation bitmap. `kfree()` checks
that the pointer is inside the block area, points at the start of a block, and
is currently marked allocated. It then clears the bitmap bit and pushes the block
onto that page's free list. Free blocks store their next pointer inside the
free block itself.

This avoids per-block headers. The invariant that one pool page contains one
size class is enough for deallocation.

## Heap Safety Rules

Heap metadata updates are protected with `irq_save()` and `irq_restore()`.
Allocation and free are not intended for interrupt handlers because allocation
may grow a pool from the page allocator and may scan pool pages in a size class.

`kzalloc()` zeroes the entire selected block, not only the requested byte count.
This makes reused blocks deterministic and avoids stale padding bytes inside
kernel objects.

`kfree(NULL)` is a no-op.

`kfree()` panics on:

- pointers outside heap pool pages
- interior block pointers
- double frees
- corrupted heap page metadata

## Pool Page Retention

When all blocks in a pool page become free, the page stays cached in its size
class. The heap does not return empty pool pages to the physical page allocator
in this milestone.

This keeps the first heap simple and avoids churn between `kmalloc()` and
`page_alloc()`. A later memory-pressure policy can reclaim fully free pool pages
or keep only a bounded number of warm pages per class.

## STM32 RTOS Connection

The STM32 RTOS lab path favors bounded kernel-owned structures and predictable
failure over unbounded host-style allocation. The physical page allocator
follows that discipline with fixed-size frames and deterministic exhaustion.
The heap extends the idea with fixed-size partition pools rather than a
first-fit heap.

Using pools here provides a different allocator flavor from a first-fit RTOS
heap: allocation within a pool is free-list based, no block coalescing is
needed, and external fragmentation is avoided inside each size class. The
tradeoff is internal fragmentation when requests round up to the next class.

## Test Evidence

The allocator scenario verifies:

- the initialized free count is nonzero
- allocated pages are aligned
- two live allocations are distinct
- a freed page is reused
- allocating all pages eventually returns `NULL`
- freeing all exhausted pages restores the original free count

The heap scenario verifies:

- lazy growth from zero heap pages
- 16-byte allocation alignment
- block reuse after free
- full-block zeroing through `kzalloc()`
- one allocation from each size class
- size-class growth when a pool page is exhausted
- oversized allocation failure
- `kfree(NULL)` no-op behavior

The QEMU smoke test checks for:

```text
milestone 11: physical page allocator
milestone 12: kernel heap
```
