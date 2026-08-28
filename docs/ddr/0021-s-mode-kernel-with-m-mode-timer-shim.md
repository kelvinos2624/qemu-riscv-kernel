# DDR 21: Run the Kernel in S-Mode with a Minimal M-Mode Timer Shim

## Status

Accepted

## Context

The kernel originally booted and ran entirely in RISC-V machine mode. That kept
early boot, traps, timer interrupts, and scheduling simple while the project
established kernel foundations. Stage 3 now needs real Sv39 hardware
translation. On RISC-V, normal virtual-memory execution is a supervisor-mode
kernel concern, not an M-mode kernel concern.

The project still intentionally uses QEMU `virt` with `-bios none`, so there is
no OpenSBI firmware to provide timer services or perform the M-mode to S-mode
handoff.

## Decision

Keep `-bios none`, but reduce M-mode to a minimal platform shim and run normal
kernel code in S-mode under an identity-mapped Sv39 page table.

M-mode owns:

- initial boot handoff
- PMP setup allowing S-mode to access RAM and MMIO
- `satp` installation before entering S-mode
- machine timer compare programming
- reflection of completed timer deadlines as supervisor timer interrupts

S-mode owns:

- normal trap handling through `stvec`
- timer tick accounting
- scheduler preemption and wakeups
- wait queues and mutexes
- heap and page allocator policy
- trace buffers
- page-table and address-space policy

S-mode requests timer programming with a machine `ecall` that passes an absolute
`mtime` deadline. M-mode programs `mtimecmp` to that deadline and returns.

When the machine timer fires, M-mode sets a supervisor timer pending event and
returns. The S-mode trap handler performs all kernel policy work.

Because S-mode `ecall` is reserved for the M-mode shim ABI, internal
kernel-thread control traps use fixed-width `ebreak` delegated to S-mode.

## Rationale

This is the honest version of enabling kernel paging. Merely constructing page
tables in M-mode would not prove that normal kernel instruction fetches, loads,
stores, traps, and interrupts execute through Sv39.

Keeping the first S-mode page table identity-mapped preserves existing C
pointers, static stacks, page allocator returns, and MMIO constants. The PR
therefore tests the privilege and translation boundary without also introducing
a higher-half kernel layout.

The M-mode shim must remain policy-free because machine traps can interrupt the
S-mode kernel even while `irq_save()` has masked S-mode interrupts. S-mode
critical sections protect S-mode kernel state with `sstatus.SIE`; they do not
protect shared structures from arbitrary M-mode mutation. The architectural
answer is to keep M-mode too small to share those structures.

Using absolute timer deadlines keeps scheduling policy in S-mode. M-mode does
not know about tick duration, missed ticks, sleepers, timeslices, or
preemption.

## Alternatives Considered

### Stay Entirely in M-Mode

This would keep the existing trap and timer path almost unchanged, but it would
not demonstrate real Sv39 kernel execution.

### Boot Under OpenSBI

OpenSBI would provide a conventional S-mode environment and timer calls. It was
not chosen because this project already owns its `-bios none` boot path and
benefits from demonstrating the privilege handoff explicitly.

### Let M-Mode Run Scheduler Timer Policy

The machine timer interrupt could call scheduler code directly, but that would
make M-mode part of the kernel policy layer. It would also invalidate the
meaning of S-mode `irq_save()` critical sections.

### Use S-Mode `ecall` for Kernel Thread Control

S-mode `ecall` cannot both be delegated to S-mode for scheduler traps and
reserved for calls into M-mode. Delegation is by exception cause, not by ABI
number. Fixed-width `ebreak` is used for internal kernel-thread control traps in
this milestone.

## Consequences

`irq_save()` and `irq_restore()` now mask `sstatus.SIE`. They protect S-mode
kernel state from S-mode interrupts and preemption.

M-mode code must stay lean. It must not inspect or mutate scheduler queues,
thread state, wait queues, mutex state, heap metadata, page allocator metadata,
trace buffers, or address-space policy.

The first kernel page table is identity-mapped. Text is mapped read/execute,
rodata read-only, writable kernel memory read/write, and MMIO read/write. Later
work can move to a higher-half layout or add finer mapping policy.

The M-mode trap path currently uses the interrupted S-mode kernel stack. This is
acceptable before U-mode exists because S-mode is always running on a kernel
stack. U-mode support must revisit this and give M-mode its own emergency stack
or avoid taking machine traps on user stacks.

## ECE350 and STM32 RTOS Connection

This maps directly to ECE350's hardware support topics: dual mode, privileged
traps, page-table base registers, TLB invalidation, and page permissions. It is
also the point where the page-table pointer becomes live hardware state rather
than a software test artifact.

The timer path mirrors the STM32 RTOS split between SysTick and PendSV: a
hardware timer creates periodic control, while scheduler policy and context
selection remain in a kernel-owned trap-return boundary.

## Evidence

The QEMU smoke tests observe:

```text
milestone 13: kernel paging
```

All current scenarios continue to pass with S-mode paging enabled, including
the scheduler synchronization scenario that depends on timer preemption.
