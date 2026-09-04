# DDR 32: User Address-Space Switching Through a Trampoline

## Status

Accepted

## Context

The first U-mode milestone proved privilege transition with temporary user
mappings in the active kernel page table. Stage 5 needs a stronger boundary:
scheduled U-mode code should run under its own user page table, while traps
must still regain a trusted S-mode execution environment.

RISC-V does not switch `satp` on trap entry. A trap from U-mode enters S-mode
while still translating through the user page table, so the earliest trap code
must be mapped in that user page table.

## Decision

Use a conventional fixed-address trampoline near the top of the Sv39 low
canonical range:

```text
USER_TRAP_CONTEXT_VA = USER_TRAMPOLINE_VA - PAGE_SIZE
USER_TRAMPOLINE_VA   = USER_SPACE_SV39_MAXVA - PAGE_SIZE
```

`USER_SPACE_TOP` remains the highest normal user-accessible byte. The
trampoline and trap-context mappings are supervisor-only support mappings with
`PTE_U` clear.

Each `user_task_t` owns its user page table, user code page, user stack page,
and one trap-context page. A scheduler `thread_t` references the user task but
does not own its memory lifetime. The thread layer supplies the per-thread
kernel stack pointer stored in the trap context, because only the thread layer
owns static kernel stacks.

On return to U-mode, C calls the trampoline return function at its high virtual
alias with explicit arguments:

```c
userret(user_satp, USER_TRAP_CONTEXT_VA);
```

The return trampoline sets `stvec` to the high trampoline entry, writes
`satp = user_satp`, executes `sfence.vma`, restores user registers from the
trap-context page, sets `sscratch = USER_TRAP_CONTEXT_VA`, and executes `sret`.

On U-mode trap entry, the trampoline swaps `sp` with `sscratch`, saves user
registers into the trap-context page, loads the kernel `satp`, kernel C trap
handler, identity-mapped trap-context pointer, normal kernel `stvec`, and
kernel stack pointer, then writes kernel `satp`, executes `sfence.vma`, restores
normal kernel `stvec`, switches to the kernel stack, and jumps to C.

## Rationale

The invariant is:

```text
U-mode runs under a user page table, but every trap reaches trusted S-mode code,
trusted S-mode stack memory, and the kernel page table before normal C executes.
```

Mapping broad kernel memory into every user page table would be simpler, but it
would make the user page table carry the kernel layout as a permanent support
contract. A trampoline keeps the shared surface narrow: one executable bridge
page and one supervisor-only per-task trap-context page.

The trap context is accessed through `USER_TRAP_CONTEXT_VA` only while the user
page table is active. Normal kernel C uses the identity-mapped physical pointer.
This keeps the fixed high support VA out of the general kernel address-space
contract.

PR1 keeps kernel-thread restore on the existing `trap_restore` path. User
thread restore is the only path that needs the trampoline.

## Consequences

`sscratch` has two U-mode meanings across milestones:

- first-user compatibility path: kernel trap-stack top
- user-`satp` trampoline path: `USER_TRAP_CONTEXT_VA`

The active `stvec` also changes by execution mode. Normal S-mode kernel
execution uses the identity-mapped `trap_entry`. Returning to U-mode sets
`stvec` to the high trampoline entry so a future U-mode trap is reachable under
the user page table.

The implementation uses coarse `sfence.vma` after every `satp` write. ASIDs are
deferred until there is a larger process model and a measured need.

Full user-task teardown is deferred. The exit syscall retires the scheduled user
thread and proves the address-space switch; later lifecycle work should reclaim
user-task frames only after no restore path can reference the trap context.

## Alternatives Considered

Mapping supervisor kernel text/data into each user page table was rejected for
this milestone because it weakens the isolation shape.

Mapping the trap-context high VA into the kernel page table was rejected because
the kernel already has an identity mapping for managed RAM. Keeping the high VA
user-page-table-only prevents normal kernel code from depending on support
mappings.

Using the existing kernel stack as the trap context was rejected because it
would expose a broad live kernel object in every user page table. The dedicated
trap-context page keeps the shared mapping intentionally small.

## Evidence

The `user-satp` scenario observes:

```text
scenario: user-satp
user: entering u-mode pc=... sp=... satp=...
user: exited code=...
milestone 22: user address-space switching
```

Run it with:

```sh
make test SCENARIO=user-satp
```

## Connections

The ECE350 connection is the dual-mode trap boundary plus the process/address
space split: the schedulable thread is not the same object as the memory
protection domain it enters.

The STM32 RTOS analogy mostly stops at synthetic context startup and interrupt
return. Cortex-M exception entry does not require this software trampoline
because there is no comparable `satp` switch to perform.
