# DDR 19: Use a Page-Backed Size-Class Kernel Heap

## Status

Accepted

## Context

The physical page allocator can allocate 4 KiB frames, but many kernel objects
are much smaller than a page. Future page-table helpers, address-space metadata,
driver requests, wait handles, and small buffers need variable-sized kernel
allocation without wasting a full physical page per object.

The STM32 RTOS project already explored first-fit style heap allocation. This
kernel should exercise a different allocator design that better matches
RTOS/kernel pool allocation patterns.

## Decision

Implement `kmalloc()`, `kzalloc()`, and `kfree()` as a lazy page-backed
size-class pool allocator.

Supported size classes are:

```text
32, 64, 128, 256, 512, 1024, 2048
```

Each pool page is one 4 KiB physical page from `page_alloc()`. A pool page serves
exactly one size class and contains a page header, allocation bitmap, free-list
head, and fixed-size blocks.

`heap_init()` allocates no pages. The heap grows lazily when `kmalloc()` needs a
class that has no free block available.

`kzalloc()` zeroes the entire selected block, not only the requested size.

Fully free pool pages remain cached in the heap. They are not returned to the
physical page allocator in this milestone.

## Rationale

Size-class pooling avoids the split/coalesce machinery and external
fragmentation behavior of a first-fit heap. Within a pool page, all blocks are
the same size, so allocation and free can use a simple free list and a per-page
bitmap.

The page-backed design makes deallocation straightforward without per-block
headers. `kfree(ptr)` rounds the pointer down to the 4 KiB page boundary, reads
the heap page header, validates the pointer against the block area and bitmap,
then pushes the block back onto that page's free list.

Lazy growth preserves physical pages for future page-table and userspace work.
Retaining fully free pool pages keeps the first heap simple and avoids churn
between the heap and physical page allocator.

## Alternatives Considered

### First-Fit Heap

First-fit supports arbitrary allocation sizes and demonstrates splitting and
coalescing. It was not chosen because the STM32 RTOS project already covered
that flavor, and this kernel benefits from exploring a more predictable
pool-oriented design.

### One Pool Per Type

Per-object pools can be very predictable, but the kernel does not yet have
stable object families. Size classes provide a useful middle ground while the
memory, VM, and driver subsystems are still forming.

### Return Empty Pool Pages Immediately

Returning a page to `page_free()` as soon as its last block is freed would reduce
heap retention, but it adds list manipulation and can cause allocator churn.
This can be revisited once real memory pressure exists.

## Consequences

Requests larger than 2048 bytes return `NULL`. Large allocations should use
whole pages directly for now or wait for a later multi-page heap policy.

The allocator trades external fragmentation for internal fragmentation. For
example, a 33-byte allocation consumes a 64-byte block.

`kmalloc()` and `kfree()` use interrupt masking for metadata consistency on the
single-hart kernel, but they are not intended for interrupt handlers.

## ECE350 and STM32 RTOS Connection

This follows the ECE350 distinction between physical page allocation and
higher-level heap allocation. The page allocator manages fixed-size frames; the
kernel heap subdivides some frames for kernel-owned objects.

It also maps to the STM32 RTOS discussion of deterministic memory management:
fixed-size partition pools avoid external fragmentation and make allocation
behavior easier to reason about than an unbounded general-purpose heap.

## Evidence

The `heap` scenario verifies lazy growth, 16-byte alignment, block reuse,
full-block zeroing through `kzalloc()`, all supported size classes, growth when a
size class exhausts a page, oversized allocation failure, and `kfree(NULL)`.

The QEMU smoke test checks for:

```text
milestone 12: kernel heap
```
