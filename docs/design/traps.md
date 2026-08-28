# Trap Design

## Scope

The kernel now boots through a minimal machine-mode shim and runs normal kernel
code in supervisor mode. M-mode handles only machine-owned platform mechanism.
The S-mode kernel owns normal traps, timer accounting, scheduler policy, and
future user/kernel boundaries.

## Trap Modes

M-mode uses direct `mtvec` with `machine_trap_entry`. This path is deliberately
lean: it handles S-mode machine calls for timer programming and reflects
machine-timer completion as a supervisor timer interrupt. It must not inspect or
mutate scheduler queues, thread state, wait queues, mutex state, heap metadata,
page allocator metadata, trace buffers, or address-space policy.

S-mode uses direct `stvec` with `trap_entry`. Normal kernel traps enter the
S-mode C trap handler, `trap_handle`, and return through `sret`.

Direct mode is intentionally simple: it gives one path to debug before the
kernel has many interrupt sources. Vectored mode can be introduced later if
timer or external interrupt latency becomes important enough to measure.

Both trap entries are explicitly 4-byte aligned because `mtvec` and `stvec`
store the trap mode in the low two bits of the CSR value.

## Trap Frame

The trap assembly saves all general-purpose registers except `x0`, plus the
active trap CSRs. In S-mode, the saved CSR slots contain `sepc`, `sstatus`,
`scause`, and `stval`. In M-mode, the same frame layout carries `mepc`,
`mstatus`, `mcause`, and `mtval`.

The C structure still uses the historical field names `mepc`, `mstatus`,
`mcause`, and `mtval`, but after the S-mode transition those slots should be
read as generic trap-frame `epc/status/cause/tval` fields. A future cleanup can
rename them without changing the layout.

The full frame costs more stores than a minimal exception-only frame, but it is
a better foundation for later syscalls, context switching, and diagnostics.
Compile-time checks in `trap.c` verify that the C structure offsets match the
assembly layout.

## Return Path

After `trap_handle` returns, the S-mode assembly restore path writes `sepc` and
`sstatus`, restores general-purpose registers from the selected frame, then
executes `sret`.

Most traps return the same frame they entered with. Timer preemption and
kernel-thread control traps may return a different thread's saved frame. This
keeps trap-frame selection in C while assembly remains responsible for the
register restore mechanics.

The self-test handles a deliberate fixed-width `ebreak`, advances the saved EPC
by 4, and returns to the instruction after the trap. The kernel uses fixed-width
`ebreak` for internal thread-control traps because S-mode `ecall` is reserved
for calls into the M-mode shim.

## Page Fault Diagnostics

S-mode decodes instruction, load, and store/AMO page faults before the generic
unexpected-trap panic path. A page fault report prints:

- access type: instruction, load, or store
- `scause`
- `sepc`
- `stval`
- `sstatus`
- `satp`
- current thread ID

This is diagnostic-only in the current milestone. The kernel does not treat any
page fault as recoverable yet, does not advance `sepc`, and does not resume
execution after the report. That keeps real kernel faults loud while the project
still lacks user mode, address-space teardown, and safe usercopy.

The `page-fault` scenario deliberately performs an unmapped load from
`0x0000000040000000`. That address is canonical under Sv39 and outside the
active identity-mapped kernel RAM/MMIO regions, so the smoke test can verify the
load-fault diagnostic without relying on recovery.

Recoverable fault probes are deferred until safe usercopy. At that point the
kernel can define a narrow contract for expected faults, identify the faulting
copy site, and return an error instead of panicking.

Unexpected S-mode traps still print CSR diagnostics and panic. Unexpected M-mode
traps also panic, because the M-mode shim has no recovery policy of its own.

## ECE350 and STM32 RTOS Connection

This follows the ECE350 distinction between hardware traps and OS policy:
hardware transfers control to a privileged handler, but the kernel decides what
the event means. Page faults are now decoded as a specific hardware exception,
but the recovery policy is intentionally deferred.

The timer/preemption path mirrors the STM32 RTOS SysTick/PendSV split. The
timer creates a controlled scheduling point, while the actual context switch
happens at trap return using a full saved context.

The page-fault scenario is unlike the STM32 RTOS lab's MPU-less heap faults:
Sv39 gives the kernel architectural fault metadata (`scause`, `sepc`, `stval`)
instead of only a generic hard-fault style failure. The shared lesson is still
the same: keep fault handling deterministic and preserve enough context to debug
the broken invariant.

## Next Work

- Add recoverable usercopy fault probes once U-mode and user mappings exist.
- Replace the kernel-only control trap path with real user syscalls once U-mode
  exists.
