# DDR 20: Build Sv39 Page-Table Primitives Before Enabling Paging

## Status

Accepted

## Context

The kernel now has a physical page allocator and a page-backed kernel heap.
Stage 3 next needs page-table creation, kernel paging, page faults, userspace
mappings, and safe usercopy. Enabling the MMU immediately would combine several
failure modes at once: malformed PTEs, bad address-space layout, missing TLB
maintenance, trap handling gaps, and incorrect user/kernel permissions.

The ECE350 virtual-memory model separates page-table construction from the act
of translating through hardware. That separation is useful here because the
kernel can first prove its Sv39 structures with deterministic software walks
while address translation remains disabled.

## Decision

Add an Sv39 page-table primitive layer before enabling hardware paging.

The layer provides:

- `vm_space_init()` for root page-table creation
- `vm_map_page()` for 4 KiB virtual-to-physical mappings
- `vm_unmap_page()` for clearing leaf mappings
- `vm_translate()` for software translation and tests

Page-table pages come from `page_alloc()` and are zero-filled before use. The
walker lazily allocates intermediate levels as mappings require them.

This milestone supports only 4 KiB leaf mappings. Superpages are deferred.

This milestone does not write `satp`, execute `sfence.vma`, invalidate TLB
state, install page-fault handling, or switch address spaces.

`vm_unmap_page()` clears the leaf PTE but does not reclaim now-empty
intermediate page-table pages. That reclamation is deliberately deferred until
the kernel has address-space teardown or a memory-pressure policy.

## Rationale

Building page tables before enabling paging creates a smaller testable unit.
The VM scenario can check canonical virtual-address validation, permission
rules, duplicate mappings, unmapped translations, sparse address ranges, and
offset-preserving translation without also debugging trap entry under paging.

Lazy intermediate allocation matches the reason multi-level tables exist: only
the page-table branches that cover live mappings consume physical pages.

Deferring empty-table reclamation keeps `unmap` simple. Correct reclamation
requires knowing whether an intermediate page-table subtree is empty, walking
back up the tree, and deciding how that interacts with future shared kernel
mappings and address-space lifetime. There is no real memory-pressure signal
yet, so the extra machinery would not pay for itself in this PR.

## Alternatives Considered

### Enable Kernel Paging Immediately

This would demonstrate hardware translation sooner, but it would merge PTE
construction, kernel virtual layout, `satp`, `sfence.vma`, TLB behavior, and
fault handling into one risky milestone.

### Use the Kernel Heap for Page Tables

Page tables are naturally page-sized and page-aligned. Allocating them directly
from `page_alloc()` is simpler and keeps their lifetime separate from small
kernel heap objects.

### Reclaim Empty Intermediate Tables on Every Unmap

Immediate reclamation would save pages after sparse unmaps, but it adds reverse
walks and subtree accounting before address-space destruction exists. This is
documented as future work rather than treated as an accidental omission.

### Add Superpage Support Now

Superpages can reduce page-table memory and TLB pressure, but 4 KiB mappings are
enough for the current userspace and driver path. Superpages can be added once
the kernel has measurable mapping pressure.

## Consequences

The VM layer consumes physical pages for root and intermediate page tables and
does not currently return intermediate page-table pages after leaf unmaps.

The API remains intentionally small. Callers can create an address space, map
pages, unmap pages, and perform software translation, but the internal
allocation-aware page-table walk is not exposed as a public contract.

Later paging PRs must add kernel virtual layout, `satp` installation,
`sfence.vma` placement, TLB invalidation policy, page-fault diagnostics,
address-space teardown, userspace mappings, and safe usercopy validation.

## ECE350 and STM32 RTOS Connection

This directly matches the ECE350 paging topics: multi-level page-table indexing,
valid versus invalid PTEs, page permissions, and preserving the page offset
during translation.

The STM32 RTOS connection is less direct because that lab targets an MCU-style
environment rather than an Sv39 MMU. The shared engineering lesson is to keep
kernel-owned memory explicit and failure behavior deterministic.

## Evidence

The `vm` scenario verifies root allocation, page alignment, mapping,
offset-preserving software translation, duplicate-map rejection, unmap and
duplicate-unmap behavior, sparse address mappings, invalid alignment, invalid
flags, and non-canonical virtual-address rejection.

The QEMU smoke test checks for:

```text
milestone 13: sv39 page table primitives
```
