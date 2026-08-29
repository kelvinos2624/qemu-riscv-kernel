# DDR 24: Prove U-Mode Transition Before Separate Address Spaces

## Status

Accepted

## Context

Stage 3 now has S-mode kernel paging, fatal page-fault diagnostics, user mapping
helpers, and a nullable per-thread address-space placeholder. The next milestone
is to prove that the kernel can enter U-mode and regain control through a trap.

The repo does not yet have a full syscall ABI, separate user `satp` switching,
a trap trampoline, process lifetime, or safe usercopy. Combining all of those
with the first privilege transition would make the failure surface too large.

## Decision

Optimize PR8 around the U-mode privilege transition.

Use the active kernel page table for the first user task and temporarily map one
user code page and one user stack page into it with user permissions. Defer
separate user page tables, `satp` switching, and trampoline isolation.

Still keep U-mode execution on a real user stack. Generalize `trap_entry` so a
U-mode-origin trap immediately switches to a kernel-owned trap stack through
`sscratch` before saving registers.

Add a tiny linked user program that performs:

```text
a0 = 0
a7 = USER_SYSCALL_EXIT
ecall
```

Delegate U-mode `ecall` to S-mode, while keeping S-mode `ecall` reserved for
the M-mode timer shim. Add a minimal user syscall helper that handles only
`USER_SYSCALL_EXIT` and returns to an S-mode continuation that prints the
first-user milestone.

## Rationale

The invariant for this milestone is:

```text
The kernel can construct a U-mode context, enter it with sret, take a U-mode
trap, save privileged state on a trusted kernel stack, and resume kernel control.
```

Using the active kernel page table keeps the PR focused on privilege mechanics.
It also avoids introducing a trampoline and address-space switch before the
first user/kernel crossing is observable.

The trap-stack decision is stricter than the page-table decision. U-mode should
execute on its own stack, but S-mode must not write its trap frame onto
user-accessible memory. `sscratch` provides the minimal hardware-supported
handoff point for that stack switch.

Because the kernel copies code bytes into a page and then executes them, the ISA
baseline now includes `zifencei`, and the scenario executes `fence.i` before
entering U-mode.

## Alternatives Considered

### Shared Kernel Page Table and User Stack Trap Frames

This would be the smallest possible U-mode proof, but it would write S-mode
trap frames onto user-accessible memory. That violates the stack-boundary
invariant even for a teaching kernel.

### Separate User Page Table With Supervisor Kernel Mappings

This would exercise PR7 address-space state more directly, but user page tables
would carry broad supervisor mappings and the PR would still need careful trap
stack handling.

### Trampoline and Separate Kernel Page Table Switch

This is the stronger general OS design. The user page table would contain only
user mappings plus a minimal shared trampoline page. It was deferred because the
current milestone is the first privilege transition, not final isolation.

## Consequences

PR8 proves U-mode execution and return-to-kernel through a delegated user
`ecall`. It does not prove separate user address-space isolation.

The active kernel page table temporarily contains user mappings for the
first-user scenario. This must not be mistaken for the final userspace memory
model.

`sscratch` is now part of the S-mode trap ABI:

- zero while executing S-mode kernel code
- kernel trap-stack top while executing U-mode

The restore path maintains this invariant when returning to either privilege
mode.

Future work must decide whether to use a separate user page table with
supervisor mappings or a trampoline-style `satp` switch before adding real
multi-task userspace and safe usercopy.

## ECE350 and STM32 RTOS Connection

This is the ECE350 dual-mode transition in executable form: the kernel prepares
a saved context, lowers privilege through trap return, and regains control
through a trap.

The STM32 RTOS connection is limited. The familiar part is synthetic context
startup, similar to preparing a task's initial exception frame. The part that
does not carry over is the U/S privilege boundary and `sscratch`-based stack
switch.

## Evidence

The QEMU smoke test observes:

```text
scenario: first-user
user: entering u-mode
user: exited code=
milestone 15: first user task
```
