# DDR 25: Defensive Usercopy With Thread-Scoped Fault Probes

## Status

Accepted

## Context

PR9 needs safe byte-granularity copies across the user/kernel boundary. The
current kernel has Sv39 enabled and can run a tiny U-mode program, but it still
uses one active kernel page table with temporary user mappings. There is no
separate process page table, address-space switch, or demand-paging policy yet.

The copy path must reject invalid user pointers before touching memory, but
validation alone is not a complete correctness story once hardware page faults
exist. A mapping can still be absent, stale, or later changed by more advanced
kernel code. The page-fault path therefore needs a narrow way to recover from
expected usercopy faults without turning normal kernel faults into silent errors.

## Decision

Safe usercopy uses validate-first semantics plus a recoverable-fault backstop.
`copy_from_user()` and `copy_to_user()` validate the full user range against the
active kernel page table, then copy under a per-thread probe. The API returns
`0` only when the entire range is copied. Invalid ranges return
`USERCOPY_ERR_INVALID`; page faults recovered by the probe return
`USERCOPY_ERR_FAULT`.

The VM layer exposes `vm_get_mapping()` as a mechanism for reading a leaf
translation and PTE flags. The usercopy layer owns the policy checks:

- user virtual address must be inside the current sparse user layout
- the range must avoid temporary QEMU `virt` MMIO-looking holes
- every touched page must be mapped with `VM_PTE_U`
- `copy_from_user()` requires readable pages
- `copy_to_user()` requires writable pages

Recoverable probes live in private per-thread state behind opaque thread APIs.
The trap handler recovers only load/store page faults when both conditions hold:

- the faulting PC is inside the armed usercopy copy loop
- `stval` is inside the user pointer range for that specific copy
- the trap cause matches the expected copy direction, load for
  `copy_from_user()` and store for `copy_to_user()`

While the copy loop is armed, usercopy enables `SSTATUS_SUM` and masks
interrupts. This keeps the current single-hart scheduler from running unrelated
kernel code while S-mode has temporary access to `U` pages.

## Consequences

This design keeps normal page faults diagnostic and fatal while allowing
expected usercopy faults to return an error. It also avoids putting user policy
inside `vm.c`; VM reports mappings, and usercopy decides whether those mappings
are acceptable for a user/kernel transfer.

The approach is conservative for this kernel stage. It validates against the
active kernel page table because PR8 temporarily maps user pages there. A later
per-process address-space implementation should move validation to the target
thread or process page table.

The copy helper is a small RISC-V assembly routine with exported start, end, and
fixup labels. That is more architecture-specific than a plain C loop, but it
gives the trap path a precise recoverable PC range and fixup address without
adding a broad exception mechanism.

Because PR9 edits mappings in the active page table, successful `vm_map_page()`
and `vm_unmap_page()` now issue a coarse `sfence.vma` once Sv39 is active. This
is a temporary single-hart policy. Future per-address-space work should revisit
whether invalidation belongs in the generic VM operation, the active address
space manager, or an ASID-aware TLB layer.

Interrupt masking around the copy loop is acceptable for this milestone because
copies are expected to be short. If larger user buffers become common, the kernel
will need to revisit this policy, likely with chunking, preemption constraints,
or a different `SUM` handling strategy.

## Connections

ECE350's protection and virtual-memory material frames usercopy as a controlled
copy across protection domains: the kernel must validate user-supplied
addresses instead of trusting them as ordinary pointers.

The STM32 RTOS lab analogy is limited. The lab's critical-section discipline
maps to masking interrupts around a short kernel-owned invariant update, but
MCU-style systems usually do not have Sv39 page faults or a `SUM`-like
supervisor/user access bit. Recoverable probes are a VM-specific backstop, not a
general embedded RTOS pattern.
