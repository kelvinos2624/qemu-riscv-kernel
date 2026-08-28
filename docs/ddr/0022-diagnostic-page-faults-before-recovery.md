# DDR 22: Decode Page Faults Before Adding Recovery

## Status

Accepted

## Context

The kernel now runs in S-mode under an identity-mapped Sv39 page table. Bad
instruction fetches, loads, and stores can therefore fault through the real
hardware translation path instead of only failing through software VM helpers.

Stage 3 still has no U-mode tasks, no user address-space lifetime model, and no
safe usercopy contract. Without those pieces, treating a fault as expected can
hide a real kernel bug.

## Decision

Decode instruction, load, and store/AMO page faults globally in the S-mode trap
handler, print a rich diagnostic, and then panic.

The diagnostic includes the access type, `scause`, `sepc`, `stval`, `sstatus`,
`satp`, and the current thread ID.

Add a dedicated `page-fault` scenario that performs an unmapped load from
`0x0000000040000000`. The QEMU smoke test treats observation of the diagnostic
as success even though the kernel does not continue after the fault.

## Rationale

This PR's invariant is observability, not recovery. The kernel should make the
fault class and hardware state clear, but it should not decide that any fault is
safe to ignore or skip yet.

Testing an unmapped load is the smallest useful hardware proof because it uses
the active kernel page table and produces a stable `stval` without needing an
executable test page or write-specific mapping setup. The trap code still
decodes instruction and store page faults so future negative tests do not need
a new architectural path.

Printing the extra CSRs is cheap on a fatal path. The added output improves
debugging and does not create a persistent memory or scheduling burden.

## Alternatives Considered

### Generic Panic Only

This would preserve the current behavior, but it would make Sv39 failures hard
to distinguish from other exceptions and would provide weak evidence for the
page-fault milestone.

### Expected-Fault Recovery Scenario

The scenario could mark a deliberate fault as recoverable, skip the faulting
instruction, and print success afterward. That was deferred because it creates
a recovery mechanism before the kernel has a narrow usercopy-style contract.

### Fault Probe API Now

A `fault_probe` or exception-table mechanism will likely be useful for
`copy_from_user()` and `copy_to_user()`. Adding it now would be premature
because there are no user mappings or syscall boundary yet.

## Consequences

All page faults remain fatal. A page fault in a normal scenario is still a
kernel bug unless that scenario is explicitly the page-fault diagnostic test.

The smoke harness can validate a scenario whose successful observation ends at
the fault diagnostic. It does not require a post-fault milestone banner.

Future PRs should add instruction and store page-fault scenarios, then introduce
recoverable probes only where the API contract says an address may fault.

## ECE350 and STM32 RTOS Connection

This aligns with the ECE350 virtual-memory model: invalid page-table entries
are meaningful hardware state, and the trap metadata tells the kernel which
virtual access failed.

The STM32 RTOS lab connection is weaker because that project did not use Sv39.
The transferable idea is deterministic fault handling: preserve enough context
to debug the broken invariant, and do not silently continue from unknown memory
corruption.

## Evidence

The QEMU smoke test observes:

```text
scenario: page-fault
trap: page fault access=load
```
