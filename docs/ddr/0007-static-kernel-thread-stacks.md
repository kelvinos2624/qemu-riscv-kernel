# DDR 7: Use Static Kernel Thread Stacks First

## Status

Accepted

## Context

The kernel needs thread stacks before it has a heap, physical page allocator, or
virtual memory manager.

## Decision

Use one statically allocated kernel stack per thread slot for the cooperative
threading milestone.

## Alternatives Considered

- Allocate stacks dynamically from a kernel heap.
- Allocate stacks from physical pages.
- Share one kernel stack across cooperative threads.

## Consequences

Static stacks are simple, deterministic, and independent of the memory
allocator. The tradeoff is fixed capacity and fixed stack memory overhead.
Dynamic stack allocation should be revisited during the memory-management
milestone.

## Evidence

`kernel/core/thread.c` defines `thread_stacks[THREAD_MAX][THREAD_STACK_SIZE]`
and constructs initial switch frames inside those stacks.
