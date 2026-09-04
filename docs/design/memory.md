# Memory Management Design

## Scope

This design note covers the memory-management work that starts the
virtual-memory and allocation section. The physical page allocator owns free RAM
after the kernel image and hands out fixed-size page frames. The kernel heap
then lazily subdivides some of those frames into smaller size-class pools for
kernel objects. The Sv39 page-table layer builds software-managed address spaces
from those same physical frames.

The kernel runs under an identity-mapped Sv39 page table, so returned page,
heap, and page-table addresses are still usable as C pointers in S-mode. Later
virtual-memory work may wrap physical-frame values in a physical-address type
once virtual and physical addresses are no longer interchangeable.

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

## Sv39 Page-Table Primitives

The virtual-memory layer exposes:

```c
int vm_space_init(vm_space_t *space);
int vm_space_destroy(vm_space_t *space);
int vm_map_page(vm_space_t *space, uintptr_t va, uintptr_t pa, uint64_t flags);
int vm_unmap_page(vm_space_t *space, uintptr_t va);
uintptr_t vm_translate(const vm_space_t *space, uintptr_t va);
```

`vm_space_t` owns one Sv39 root page table. The root and all intermediate
page-table pages come directly from `page_alloc()` and are zero-filled before
use. The page-table walker allocates missing intermediate levels lazily when
mapping a virtual address.

The implementation supports three Sv39 levels, 512 entries per table, 9-bit VPN
indices, and 4 KiB leaf mappings. It intentionally does not support superpages
yet. Branch PTEs contain only `V`; leaf PTEs must contain `V` plus a readable or
executable permission. Writable mappings must also be readable, matching the
RISC-V PTE rule.

`vm_translate()` is a software walk used for tests and future kernel helpers.
It returns the mapped physical page address plus the original 12-bit page
offset, or `VM_TRANSLATE_INVALID` when the virtual address is not mapped.

The page-table primitive milestone built and validated page-table data
structures while paging was still disabled. It did not write `satp`, execute
`sfence.vma`, invalidate TLB state, install page-fault handling, or switch
kernel/user address spaces.

`vm_unmap_page()` clears only the leaf PTE. Empty intermediate page-table pages
are deliberately not reclaimed in this milestone. Deferring reclamation keeps
the first VM primitive small and avoids recursive subtree accounting before
there is real address-space teardown or memory pressure. A future address-space
destroy path can reclaim page-table pages with a post-order walk.

After paging is active, successful `vm_map_page()` and `vm_unmap_page()` issue a
coarse `sfence.vma`. This is intentionally simple for the current single-hart,
single-active-address-space kernel. A later address-space switch or ASID design
can replace it with narrower invalidation policy.

Failed `vm_map_page()` calls are not allowed to retain newly allocated
intermediate page-table pages. If a sparse mapping allocates part of a fresh
branch and then runs out of physical pages, the walker clears the PTEs it
installed during that call and returns those pages to `page_alloc()`.

`vm_space_destroy()` reclaims page-table structure pages only. It first scans
the tree and refuses destruction with `VM_ERR_BUSY` if any valid leaf mapping is
still present. This keeps physical-frame ownership outside the generic VM layer:
callers must unmap leaf pages and release any frames they own before destroying
the translation structure.

Destroying an already-empty address space frees intermediate page-table pages
with a post-order walk, frees the root page, and clears `space->root`.

## User Address-Space Skeleton

The user-address-space layer is a small policy module over `vm_space_t`, not a
separate process or address-space object. `vm.c` remains the generic Sv39
mechanism; `user_space.c` owns the current user mapping policy.

The initial sparse user layout is:

```text
0x0000000000000000 - 0x0000000000000fff   null guard, unmapped
0x0000000000001000                         first user code page
...
0x000000003ffff000                         initial user stack page
0x0000000040000000                         user stack top / user top
```

The skeleton maps only one code page and one stack page today. The larger
sparse range preserves a realistic code-low/stack-high mental model for PR8
without requiring a full loader, heap, or process layout.

User mapping helpers enforce:

- virtual address is page-sized and inside the user range
- virtual address does not overlap the current QEMU `virt` CLINT or UART MMIO
  holes
- physical address is inside allocator-managed RAM
- `VM_PTE_U` is present
- `VM_PTE_G` is absent

The MMIO virtual-address holes are a temporary QEMU `virt` layout guard. They
avoid confusing user VAs that visually overlap the identity-mapped kernel MMIO
layout. The longer-term isolation invariant is physical ownership: users should
not be able to map kernel-owned or device-owned frames unless a later kernel API
grants that access explicitly.

The managed-RAM PA check rejects obvious kernel-image, CLINT, and UART physical
addresses. It is not complete ownership tracking: a managed frame may still be
owned by another kernel subsystem. Callers remain responsible for passing pages
they actually own until the kernel has frame ownership metadata.

PR8 temporarily maps the first U-mode code and stack pages into the active
kernel page table with `VM_PTE_U`. This is deliberately scoped to proving the
U-mode privilege transition and delegated user `ecall`; it does not claim
separate address-space isolation. The lower user aliases are backed by
allocator-owned pages and are still also reachable through the kernel's
identity mapping for setup.

Because the kernel writes the first user program into a data page and then
executes it, the target ISA includes `zifencei` and the scenario executes
`fence.i` before entering U-mode.

## Safe Usercopy

The safe-usercopy layer exposes:

```c
int copy_from_user(void *dst, const void *user_src, size_t len);
int copy_to_user(void *user_dst, const void *src, size_t len);
```

Both functions use an all-or-error contract: they return `0` only when the full
range was copied. Invalid user ranges and permission failures return
`USERCOPY_ERR_INVALID`; a recoverable page fault during the guarded copy returns
`USERCOPY_ERR_FAULT`. Destination contents are unspecified after failure.

Usercopy is defensive by construction. It first validates the entire user range
against the active kernel page table because the current PR8/PR9 kernel still
temporarily maps user pages there. The usercopy policy checks that every page in
the range:

- is inside the sparse user VA layout
- avoids the temporary QEMU `virt` MMIO-looking holes
- has a present leaf mapping
- has `VM_PTE_U`
- has `VM_PTE_R` for `copy_from_user()` or `VM_PTE_W` for `copy_to_user()`

`vm_get_mapping()` is the generic VM mechanism that reports a leaf translation
and its flags. The usercopy layer owns the user/kernel policy built on top of
that mechanism.

After validation, usercopy briefly enables `SSTATUS_SUM` so S-mode can touch
user pages. Interrupts stay masked across the `SUM` window and copy probe. This
preserves the single-hart invariant that no other kernel thread runs while the
kernel has temporary supervisor access to user mappings.

Recoverable fault probes are per-thread state accessed through opaque thread
APIs. The trap handler recovers only load/store page faults whose saved program
counter is inside the armed copy loop and whose `stval` lies inside the intended
user pointer range. `copy_from_user()` recovers only load page faults;
`copy_to_user()` recovers only store page faults. This keeps usercopy robust
against stale translations or future races while still allowing unrelated kernel
faults inside the copy helper to panic normally.

The current implementation supports cross-page copies. It does not yet validate
against a per-process page table because separate user address spaces are not
active yet.

## Stage 4 Driver Readiness Notes

Stage 4 should treat the Stage 3 memory work as a set of mechanisms and
invariants, not as a final device-memory model. The driver framework and
simulated accelerator should preserve this core rule:

```text
A device may operate only on kernel-validated, explicitly owned physical memory.
```

The current identity-mapped kernel page table makes early driver work easier:
kernel virtual addresses equal physical addresses for managed RAM and MMIO.
That is a QEMU/simple-kernel convenience, not a general DMA or IOMMU model.
Driver code should keep physical-address boundaries explicit so a later
non-identity kernel map or userspace address-space switch does not silently
break device submission.

`page_alloc()` is the safest first source for device-visible command
descriptors, descriptor rings, and data buffers. It returns 4 KiB aligned frames
inside allocator-managed RAM. `kmalloc()` is appropriate for ordinary
byte-granularity kernel objects, but Stage 4 should not assume every heap block
is a durable device buffer. If a driver uses heap memory for device-facing
state, the ownership and physical-address conversion rules must be documented
at the call site.

`page_range_is_managed_page_contained()` is a narrow mechanism for early driver
validation. It proves that a nonempty physical range lies inside the
allocator-managed RAM window and does not cross a 4 KiB page boundary. It does
not prove that the caller currently owns the frame. The accelerator descriptor
API still requires callers to allocate and retain the pages they submit.

User virtual addresses must never be passed directly to the simulated device.
Kernel-only driver scenarios can begin with kernel-owned buffers. The initial
Stage 5 syscall ABI intentionally carries only integer arguments. Later
userspace-facing driver syscalls should use `copy_from_user()` and
`copy_to_user()` for descriptor-sized metadata, then validate and pin or copy
payload buffers before constructing device command descriptors.

`vm_get_mapping()` can help inspect the active page table for mapped physical
addresses and PTE flags, but it is only a translation mechanism. Driver policy
must still decide whether the calling subsystem owns the frame and whether that
frame is legal for device access. The current user-space helpers reject
physical addresses outside managed RAM, but managed RAM is not the same thing as
uncontended ownership.

Page faults remain fatal except for the narrow usercopy probe path. Driver
faults should therefore be treated as kernel bugs unless a future driver API
defines its own explicit recovery contract.

Successful live page-table edits currently issue a coarse `sfence.vma`. That is
acceptable for the single-hart kernel while Stage 5 is still using full TLB
flushes on user/kernel address-space transitions. Per-address or ASID-aware
invalidation should wait until the kernel has enough concurrent address-space
activity to make that policy worth the complexity.

The STM32 RTOS connection is strongest around ISR-to-thread handoff: a device
interrupt should acknowledge hardware state, update small driver-owned
completion state, and wake waiters. The RISC-V/QEMU kernel adds virtual-memory
concerns that the STM32 lab did not need: physical buffer validation, usercopy,
and keeping device-visible ownership separate from ordinary C pointer use.

## Kernel Paging

The kernel now enables Sv39 and runs normal kernel code in S-mode under an
identity-mapped kernel page table. The M-mode bootstrap builds the page table,
installs it in `satp`, executes `sfence.vma`, and enters `supervisor_main()`
with `mret`.

Identity mapping is a deliberate first paging step. Virtual addresses equal
physical addresses for kernel text, rodata, writable kernel memory, allocator
managed RAM, and MMIO. This preserves existing C pointers, static thread
stacks, page allocator returns, and device constants while proving that normal
kernel instruction fetches, loads, stores, traps, and timer interrupts work
through hardware translation.

Linker symbols define the permission boundaries:

- text: read/execute/accessed
- rodata: read/accessed
- data, bss, allocator metadata, stacks, and free RAM: read/write/accessed/dirty
- UART and CLINT MMIO: read/write/accessed/dirty

The current RISC-V page-table flags do not encode rich memory attributes, so the
MMIO mappings are documented as QEMU/simple-platform mappings. A later platform
layer can add stronger device-memory policy if the target changes.

The active kernel page table is global for all current kernel threads. There is
no per-thread address-space switch yet and no user page table. S-mode page
faults are decoded and reported with architectural trap metadata. Most remain
fatal; safe usercopy is the one narrow recoverable-fault contract. Usercopy
validates ranges before copying and recovers only matching load/store faults
inside its armed copy probe.

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

Sv39 page tables go beyond the STM32 RTOS lab's MCU-style memory model. The
connection is mostly conceptual: keep memory ownership explicit, keep failure
paths deterministic, and use short interrupt-masked critical sections while the
kernel is still single-hart.

The ECE350 paging model maps directly onto this layer: virtual page numbers
select page-table entries, invalid entries represent unmapped addresses that
will later fault, and the page offset is copied unchanged through translation.
Kernel paging adds the live page-table base register and TLB-flush step from
the same notes: the `satp` write selects the page table, and `sfence.vma`
orders the transition before S-mode executes through Sv39.

## Test Evidence

The allocator scenario verifies:

- the scenario starting free count is nonzero after kernel paging setup
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

The VM scenario verifies:

- root page-table allocation and alignment
- lazy intermediate page-table allocation
- 4 KiB page mapping and offset-preserving translation
- duplicate mapping rejection
- unmap and duplicate-unmap behavior
- sparse virtual-address mappings that require separate page-table branches
- rollback of partially allocated page-table branches on `VM_ERR_NO_MEMORY`
- invalid alignment, invalid flags, and non-canonical virtual-address rejection

The page-fault scenario verifies:

- an unmapped Sv39 load faults under the active kernel page table
- the trap path identifies the fault as a load page fault
- the diagnostic includes the trap CSRs and current execution context

The user-space scenario verifies:

- code and stack pages can be mapped through user-space policy helpers
- the null guard is rejected
- missing `VM_PTE_U` is rejected
- QEMU `virt` MMIO-looking user VAs are rejected
- physical addresses outside managed RAM are rejected
- global user mappings are rejected
- `vm_space_destroy()` refuses live leaf mappings
- unmap/free/destroy restores the starting free-page count

The first-user scenario verifies:

- a tiny linked user program is copied into a user-executable page
- the initial U-mode stack pointer is the top of the user stack page
- `sret` enters U-mode
- U-mode `ecall` traps back to S-mode
- the exit syscall path returns to a kernel continuation

The usercopy scenario verifies:

- valid `copy_from_user()` across two user pages
- valid `copy_to_user()` across two user pages
- null-guard, overflow, unmapped, and MMIO-looking ranges are rejected
- writes to read-only user mappings are rejected
- the private recoverable-fault selftest returns `USERCOPY_ERR_FAULT`

The QEMU smoke test checks for:

```text
milestone 11: physical page allocator
milestone 12: kernel heap
milestone 13: sv39 page table primitives
trap: page fault access=load
milestone 14: user address space skeleton
milestone 15: first user task
milestone 16: safe usercopy
```
