# qemu-rtos

`qemu-rtos` is a hardware-adjacent RISC-V kernel and driver-stack project
running on QEMU. The goal is to build a small but defensible operating-system
kernel that demonstrates boot flow, interrupts, scheduling, memory management,
syscalls, MMIO drivers, userspace/runtime boundaries, tracing, and performance
evaluation.

This is not intended to become a POSIX-compatible OS or a Linux clone. The
project is scoped as a systems engineering portfolio piece: small enough to
explain deeply, but real enough to exercise the hardware/software boundary.

## Project Status

Progress: `[###################-] 96%`

The kernel currently boots on QEMU `virt` with `-bios none`, builds an
identity-mapped Sv39 kernel page table, enters S-mode, initializes UART output,
installs an S-mode trap path, handles reflected supervisor timer interrupts,
and runs preemptive FIFO round-robin kernel threads using trap-frame-based
switching. It also supports tick-based `thread_sleep()` through an
absolute-deadline sleep queue and event-style blocking through wait queues. It
now includes non-recursive kernel mutexes with FIFO owner transfer,
timeout-aware blocking, structured scheduler tracing, a bitmap physical page
allocator, a page-backed size-class kernel heap, Sv39 page-table primitives,
hardware kernel paging, diagnostic S-mode page-fault handling, a minimal
U-mode task, delegated user `ecall` exit, and safe usercopy with narrowly
recoverable page-fault probes. Stage 4 is complete with typed MMIO helpers,
immutable platform device resources, a bounded runtime device registry,
boot-time built-in driver probing, a simulated accelerator register model,
allocator-backed accelerator command descriptors for kernel-submitted `MEMSET`
work, interrupt-driven completion through simulated IRQ dispatch, and explicit
timeout/error handling with reset-required recovery.

## Project Goals

The project is designed to show practical understanding of:

- RISC-V boot and linker-controlled kernel layout
- freestanding C and assembly without libc or host OS dependencies
- trap and interrupt handling
- timer-driven preemption
- kernel threads, wait queues, mutexes, and blocking wakeups
- physical page allocation and virtual memory
- user/kernel pointer validation
- syscall ABI design
- MMIO device access and interrupt-driven driver completion
- DMA-style buffer ownership at the device boundary
- structured tracing, integration tests, and latency measurements

## Target Platform

- Architecture: RISC-V 64-bit
- Machine: QEMU `virt`
- Initial privilege mode: machine mode bootstrap, supervisor mode kernel
- ISA baseline: `rv64imac_zicsr_zifencei`
- Firmware: none, using QEMU `-bios none`
- Kernel load address: `0x80000000`
- Console: QEMU `virt` 16550 UART at `0x10000000`

Machine mode is used as a minimal bootstrap and platform shim. Normal kernel
execution runs in supervisor mode under an identity-mapped Sv39 page table and
can enter a minimal U-mode task. Separate user `satp` switching remains a later
milestone.

## Current Status

Milestone 1 is complete:

- freestanding RISC-V kernel image
- assembly boot entry
- linker script with kernel symbols
- boot stack setup
- `.bss` clearing
- UART console output
- panic path
- QEMU boot test that validates the milestone banner

Trap-vector setup is complete:

- minimal direct machine-mode trap vector for platform shim events
- direct supervisor-mode trap vector for normal kernel traps
- full trap frame for general-purpose registers and trap CSRs
- C trap handlers
- controlled fixed-width `ebreak` self-test for S-mode trap return

Timer interrupt setup is complete:

- QEMU `virt` machine timer MMIO
- `mtimecmp` programming
- bare-metal M-mode timer shim
- S-mode absolute-deadline timer programming through machine `ecall`
- machine-timer completion reflected as a supervisor timer interrupt
- 1 ms kernel tick on the QEMU `virt` 10 MHz timebase
- `mie.MTIE`, `mideleg.STIP`, `sie.STIE`, and `sstatus.SIE` enablement
- S-mode timer interrupt dispatch through the trap handler
- monotonic kernel tick counter

Kernel thread scheduling setup is complete:

- `thread_init()` / `thread_start()` lifecycle
- reserved TID 0 null task
- static per-thread kernel stacks
- bounded FIFO ready queue over real thread IDs
- S-mode fixed-width `ebreak` path for kernel thread control traps
- timer-driven preemptive FIFO round-robin
- trap-frame-based thread switching on trap return
- queue membership tracking to support timeout-aware blocking
- `thread_sleep(ticks)` with a sorted absolute-deadline sleep queue
- FIFO wait queues for event-style blocking and wakeups
- non-recursive kernel mutexes built on wait queues
- timeout-aware waits and `mutex_lock_timeout()`
- structured in-memory scheduler traces with explicit UART dump
- documented priority-inversion limitation for future priority scheduling
- `thread_exit()` for retiring finished threads
- 10 tick / 10 ms scheduler quantum
- interrupt masking around scheduler state

Stage 3 virtual memory and allocation foundations are complete:

- linker-provided RAM bounds for the current QEMU `virt` memory contract
- 4 KiB physical page granularity matching RISC-V Sv39 base pages
- linker-reserved allocator bitmap metadata
- bitmap-backed `page_alloc()` / `page_free()`
- free-count and managed-range introspection
- invalid-free and double-free detection through kernel panics
- interrupt masking around allocator metadata
- boot self-test for alignment, reuse, exhaustion, and count restoration
- lazy page-backed size-class kernel heap
- `kmalloc()` / `kzalloc()` / `kfree()`
- heap stats for page count, free bytes, and allocated bytes
- heap scenario coverage for alignment, reuse, zeroing, growth, and oversized
  allocation failure
- Sv39 root page-table allocation from the physical page allocator
- lazy intermediate page-table allocation
- 4 KiB page map, unmap, and software translation helpers
- canonical virtual-address, alignment, duplicate-map, and permission checks
- documented deferral of empty intermediate page-table reclamation
- identity-mapped kernel page table installed in `satp`
- S-mode kernel execution under Sv39
- linker-section permissions for text, rodata, writable kernel memory, and MMIO
- policy-free M-mode shim boundary documented for timer delivery
- instruction/load/store page-fault decoding in the S-mode trap path
- fatal page-fault diagnostics with `scause`, `sepc`, `stval`, `sstatus`,
  `satp`, and current TID
- dedicated unmapped-load page-fault smoke scenario
- user mapping helpers over `vm_space_t` for a sparse code-low/stack-high
  virtual layout
- nullable per-thread address-space placeholder for future U-mode tasks
- page-table structure teardown through `vm_space_destroy()`
- documented separation between page-table ownership and mapped-frame ownership
- general S-mode trap entry support for user-origin traps through `sscratch`
- first U-mode task entered with `sret`
- minimal U-mode `exit` syscall path through delegated user `ecall`
- temporary first-user mappings in the active kernel page table
- safe usercopy routines with validation-before-copy semantics
- per-thread recoverable fault probes for usercopy load/store faults
- `SSTATUS_SUM` enabled only inside the interrupt-masked copy window
- cross-page usercopy scenario coverage

Stage 4 driver framework and simulated accelerator work is complete:

- typed volatile MMIO helpers for 8/16/32/64-bit register access
- explicit RISC-V fence helpers and semantic driver-ordering wrappers
- immutable platform resource descriptions supplied by the RISC-V platform
  layer
- bounded runtime device registry separate from platform resource facts
- static built-in driver table with compatible-string probing
- bind-after-success driver ownership invariant
- lookup by exact device name and first matching compatible string
- IRQ metadata and driver callback shape, with dispatch deferred
- simulated accelerator platform MMIO resource and stateless driver probe
- driver-framework smoke scenario for accelerator binding and MMIO helper behavior
- accelerator ID, status, control, IRQ status, and IRQ ack register model
- synchronous register-model completion through a platform simulation hook
- deterministic reset, done, error, and IRQ acknowledgement behavior
- accelerator command descriptor ABI and synchronous kernel submission API
- allocator-managed page-contained descriptor and destination validation
- simulated device execution of descriptor-backed `MEMSET`
- explicit descriptor status for pending, success, invalid, error, and rejected
  commands
- simulated platform IRQ dispatch through bound driver callbacks
- interrupt-driven accelerator completion using a driver-owned request slot and
  wait queue
- ISR acknowledgement of accelerator completion/error IRQ status
- timeout-aware accelerator submission API
- descriptor timeout status and `ACCEL_ERR_TIMEOUT` reporting
- reset-required recovery policy after timed-out accelerator requests
- late accelerator IRQ acknowledgement after timeout without descriptor result
  rewrite

The next memory-related milestones are separate user address-space switching,
syscall ABI growth, and a less temporary process/runtime model. The next
project milestone is Stage 5 userspace runtime work.

Common boot output:

```text
qemu-rtos: booting RISC-V kernel
kernel_start=0x0000000080000000 kernel_end=... stack_top=...
milestone 1: boot, stack, bss, uart console
paging: satp=...
milestone 13: kernel paging
trap: stvec=...
trap: self-test passed
milestone 2: trap vector setup
timer: interval=...
timer: observed ... ticks
milestone 2: timer interrupt setup
```

Allocator scenario output:

```text
scenario: allocator
page: managed_start=... managed_end=... total=...
milestone 11: physical page allocator
```

Heap scenario output:

```text
scenario: heap
heap: pages=... free=... allocated=...
milestone 12: kernel heap
```

VM scenario output:

```text
scenario: vm
vm: root=... free_pages=...
milestone 13: sv39 page table primitives
```

Page-fault scenario output:

```text
scenario: page-fault
trap: page fault access=load scause=... sepc=... stval=... sstatus=... satp=... tid=...
```

User-space scenario output:

```text
scenario: user-space
user: code=0x0000000000001000 stack=0x000000003ffff000 top=0x0000000040000000
milestone 14: user address space skeleton
```

First-user scenario output:

```text
scenario: first-user
user: entering u-mode pc=0x0000000000001000 sp=0x0000000040000000
user: exited code=0x0000000000000000
milestone 15: first user task
```

Usercopy scenario output:

```text
scenario: usercopy
usercopy: passed
milestone 16: safe usercopy
```

Driver-framework scenario output:

```text
scenario: driver-framework
driver: accelerator device bound
driver: accelerator mmio path passed
milestone 17: driver framework
```

Accelerator-register scenario output:

```text
scenario: accelerator-registers
accel: reset idle
accel: start done
accel: invalid transition error
accel: reset priority passed
milestone 18: simulated accelerator registers
```

Accelerator-descriptor scenario output:

```text
scenario: accelerator-descriptors
accel: descriptor memset passed
accel: descriptor validation passed
accel: descriptor lifecycle rejection passed
milestone 19: accelerator descriptors
```

Accelerator IRQ-completion scenario output:

```text
scenario: accelerator-irq-completion
accel: submitter blocked before irq
accel: competing submit rejected
accel: irq completion woke submitter
accel: reset allows descriptor reuse
accel: spurious irq ack passed
milestone 20: interrupt-driven accelerator completion
```

Accelerator timeout/error-handling scenario output:

```text
scenario: accelerator-timeout-error-handling
accel: zero timeout rejected before start
accel: stuck request timed out
accel: reset required after timeout
accel: late irq after timeout acked
accel: timed submit completed before timeout
accel: invalid command error passed
milestone 21: accelerator timeout/error handling
```

Scheduler/synchronization scenario output:

```text
scenario: scheduler-sync
thread: initialized static table, null tid=0
thread: starting scheduler
thread: mutex-a locking
thread: mutex-a acquired
thread: mutex-b timed wait
thread: null idle
thread: mutex-b timed out
thread: mutex-a unlocking
thread: mutex-c locking
thread: mutex-c acquired
milestone 10: scheduler tracing
trace: begin count=... overwrites=...
trace: seq=... tick=... type=context_switch tid=... other=... arg0=...
trace: ...
trace: end
```

## Planned Architecture

The system will grow through five major deliverables.

1. Kernel foundations
   Boot, stack setup, UART, panic handling, trap vector setup, timer interrupt
   setup, and basic kernel thread state.

2. Scheduling and synchronization
   Preemptive round-robin scheduling, sleep queues, wait queues, mutexes,
   timeout-aware blocking wakeups, scheduler tracing, and documented
   priority-inversion prerequisites.

3. Virtual memory and allocation
   Physical page allocation, kernel heap support, page-table creation,
   userspace mappings, page faults, and safe usercopy routines.

4. Driver framework and simulated accelerator
   MMIO helpers, interrupt registration, driver-private state, blocking driver
   completion, and a simulated accelerator using command descriptors.

5. Userspace runtime, tracing, and evaluation
   Syscall wrappers, userspace startup, accelerator runtime APIs, structured
   kernel traces, integration tests, and latency/throughput measurements.

## Simulated Accelerator Direction

Stage 4 uses a small simulated hardware accelerator rather than a fake GPU,
TPU, NIC, or storage controller. The device exposes MMIO registers and accepts
command descriptors. The current operation set is intentionally narrow:
allocator-backed kernel callers can submit one page-contained `MEMSET`
operation, then wait for interrupt-driven completion or timeout.

The point of this subsystem is to demonstrate driver concepts that transfer
across many hardware domains:

- register-level MMIO access
- command descriptor ownership
- physical-address device boundaries
- interrupt-driven completion
- blocking waits versus polling
- invalid command and invalid buffer handling
- timeout and recovery paths

Userspace will submit accelerator work through syscalls and a thin runtime
library. It will not access MMIO directly.

## Deferred Scheduling Extension

Priority inversion is intentionally not demonstrated in the current scheduler.
The kernel uses preemptive FIFO round-robin scheduling and has no thread
priority model, so there is no real priority relationship to invert.

A future priority-scheduling extension should add:

- static or dynamic thread priorities
- priority-aware ready queues
- priority-aware mutex waiter selection
- priority inheritance or priority ceiling experiments

The existing mutex ownership model and scheduler trace buffer provide the
foundation for that work, but implementing a fake inversion demo on top of
round-robin scheduling would be technically misleading.

## Repository Layout

```text
.
|-- Makefile
|-- linker.ld
|-- docs/
|   |-- design/
|   |-- ddr/
|   `-- results/
|-- kernel/
|   |-- arch/riscv64/
|   |-- core/
|   |-- drivers/
|   |-- memory/
|   `-- user/
`-- tools/
```

Design notes live under `docs/design/`. Design Decision Records live under
`docs/ddr/`.

## Build and Run

Install the expected tools on macOS:

```sh
brew install qemu riscv64-elf-gcc
```

The `Makefile` auto-detects either `riscv64-unknown-elf-` or
`riscv64-elf-`. Override `CROSS_COMPILE` if your toolchain uses a different
prefix.

Build the kernel:

```sh
make
```

The default build uses `SCENARIO=scheduler-sync`.

Run it in QEMU:

```sh
make run
```

Run all current QEMU smoke-test scenarios:

```sh
make test
```

Run one scenario:

```sh
make test SCENARIO=allocator
make test SCENARIO=heap
make test SCENARIO=vm
make test SCENARIO=page-fault
make test SCENARIO=user-space
make test SCENARIO=first-user
make test SCENARIO=usercopy
make test SCENARIO=driver-framework
make test SCENARIO=accelerator-registers
make test SCENARIO=accelerator-descriptors
make test SCENARIO=accelerator-irq-completion
make test SCENARIO=accelerator-timeout-error-handling
make test SCENARIO=scheduler-sync
```

Useful targets:

```sh
make run
make debug
make test
make test SCENARIO=allocator
make test SCENARIO=heap
make test SCENARIO=vm
make test SCENARIO=page-fault
make test SCENARIO=user-space
make test SCENARIO=first-user
make test SCENARIO=usercopy
make test SCENARIO=driver-framework
make test SCENARIO=accelerator-registers
make test SCENARIO=accelerator-descriptors
make test SCENARIO=accelerator-irq-completion
make test SCENARIO=accelerator-timeout-error-handling
make test-stage4
make clean
make toolcheck
```

Tracing is compiled in by default. Build without tracing:

```sh
make clean
make CONFIG_TRACE=0
```

## Testing Strategy

The project favors repeatable tests over manual observation. The first test is a
black-box QEMU integration test that validates scenario-specific milestones
through the kernel's UART output.

Scenarios split common kernel bring-up from focused test workloads. This keeps
future page-table, user-mode, and driver tests from growing into one fragile
scripted boot path.

The current scenarios are:

- `allocator`: validates the physical page allocator self-test milestone
- `heap`: validates the kernel heap self-test milestone
- `vm`: validates Sv39 page-table primitives in a separate software-managed
  address space
- `page-fault`: validates fatal S-mode page-fault diagnostics for an unmapped
  load
- `user-space`: validates user mapping policy, sparse layout constants, and
  address-space teardown rules
- `first-user`: validates `sret` into U-mode and delegated user `ecall` exit
- `usercopy`: validates safe usercopy validation, cross-page copies, and
  recoverable usercopy fault probes
- `scheduler-sync`: validates timeout-aware mutex blocking and selected
  scheduler trace events
- `driver-framework`: validates platform/device/driver boundary separation,
  boot-time compatible accelerator binding, and typed MMIO helper behavior
- `accelerator-registers`: validates the simulated accelerator register state
  machine, reset priority, IRQ acknowledgement, and invalid transition behavior
- `accelerator-descriptors`: validates descriptor-based `MEMSET` submission,
  allocator-managed page-contained descriptor and buffer validation, and
  lifecycle rejection before reset
- `accelerator-irq-completion`: validates simulated IRQ dispatch, ISR
  completion, wait-queue wakeup, competing-submit rejection, reset-before-reuse,
  and spurious IRQ acknowledgement
- `accelerator-timeout-error-handling`: validates zero-timeout rejection, stuck
  request timeout, reset-required recovery, late IRQ cleanup, timed successful
  completion, and invalid command reporting

Stage 3 evidence matrix:

| PR | Capability | Scenario | Smoke marker | Command |
| --- | --- | --- | --- | --- |
| PR1 | Physical RAM ownership and page allocation | `allocator` | `milestone 11: physical page allocator` | `make test SCENARIO=allocator` |
| PR2 | Selectable scenario harness | all scenarios | scenario-specific UART sequence | `make test` |
| PR3 | Page-backed kernel heap | `heap` | `milestone 12: kernel heap` | `make test SCENARIO=heap` |
| PR4 | Sv39 page-table primitives | `vm` | `milestone 13: sv39 page table primitives` | `make test SCENARIO=vm` |
| PR5 | S-mode kernel paging | all scenarios | `milestone 13: kernel paging` | `make test` |
| PR6 | Fatal page-fault diagnostics | `page-fault` | `trap: page fault access=load` | `make test SCENARIO=page-fault` |
| PR7 | User address-space skeleton | `user-space` | `milestone 14: user address space skeleton` | `make test SCENARIO=user-space` |
| PR8 | First U-mode task and exit syscall | `first-user` | `milestone 15: first user task` | `make test SCENARIO=first-user` |
| PR9 | Safe usercopy and recoverable copy faults | `usercopy` | `milestone 16: safe usercopy` | `make test SCENARIO=usercopy` |
| PR10 | Stage 3 documentation and evidence cleanup | all scenarios | all current scenario markers | `make test` |

Stage 4 evidence matrix:

| PR | Capability | Scenario | Smoke marker | Command |
| --- | --- | --- | --- | --- |
| PR1 | MMIO and driver registration foundations | `driver-framework` | `milestone 17: driver framework` | `make test SCENARIO=driver-framework` |
| PR2 | Simulated accelerator register model | `accelerator-registers` | `milestone 18: simulated accelerator registers` | `make test SCENARIO=accelerator-registers` |
| PR3 | Command descriptor format and kernel submission API | `accelerator-descriptors` | `milestone 19: accelerator descriptors` | `make test SCENARIO=accelerator-descriptors` |
| PR4 | Interrupt-driven completion path | `accelerator-irq-completion` | `milestone 20: interrupt-driven accelerator completion` | `make test SCENARIO=accelerator-irq-completion` |
| PR5 | Timeout and error handling | `accelerator-timeout-error-handling` | `milestone 21: accelerator timeout/error handling` | `make test SCENARIO=accelerator-timeout-error-handling` |
| PR6 | Stage 4 integration cleanup | all Stage 4 scenarios | milestones 17-21 | `make test-stage4` |

The current tests verify that the allocator initializes and survives its boot
self-test, the heap lazily grows size-class pools and reuses/zeroes blocks, the
VM layer maps/unmaps/translates sparse pages while rejecting invalid requests,
the common boot path reaches S-mode with Sv39 enabled, an unmapped load produces
a load page-fault diagnostic, user-space mappings reject invalid VA/PA/flag
combinations and restore page counts after teardown, the kernel enters one
U-mode task and handles its exit syscall, safe usercopy validates ranges before
copying, cross-page usercopy succeeds, recoverable usercopy faults return an
error, one thread times out while waiting for a mutex, the idle task runs while
all real threads are blocked, a later thread can still acquire the mutex after
the owner unlocks, the trace dump includes key events such as context switches,
idle entry, wait timeout, and mutex timeout, and the driver framework binds a
simulated accelerator device by compatible string before validating MMIO-backed
driver operations. The accelerator-register scenario proves reset-to-idle,
synchronous start-to-done, IRQ acknowledgement without state reset, invalid
start-after-done error handling, and reset priority over start. The
accelerator-descriptor scenario proves allocator-backed descriptor submission,
safe page-contained execution, invalid descriptor/range rejection, and
deterministic lifecycle rejection before reset. The accelerator IRQ-completion
scenario proves that descriptor completion can block on driver-owned request
state, wake through the simulated IRQ dispatch and ISR path, reject a competing
submitter while the slot is owned, preserve reset-before-reuse behavior, and
ack a spurious accelerator IRQ without an active request. The accelerator
timeout/error scenario proves immediate timeout before start, timeout of a
stuck request, reset-required recovery before public lifecycle reuse, late IRQ
cleanup without rewriting the timed-out descriptor result, successful completion
before a nonzero timeout, and invalid command reporting through the timed API.

Planned test categories:

- boot and trap integration tests
- scheduler fairness and preemption tests
- allocator invariant tests
- syscall validation and negative tests
- driver completion and timeout tests
- benchmark workloads for syscall, context switch, interrupt, and accelerator
  latency

## Documentation Strategy

Each major subsystem should include:

- a short design note under `docs/design/`
- a Design Decision Record under `docs/ddr/` for important tradeoffs
- tests or traces that provide evidence for the chosen design

The project should be explainable in interviews from first principles: what the
subsystem solves, what constraints shaped it, which alternatives were rejected,
how it fails, how it is tested, and what is measured.

## Non-Goals

The initial project does not aim to:

- run Linux binaries
- implement a POSIX-compatible userspace
- support multiprocessing
- provide a production filesystem
- model a real commercial accelerator
- optimize before behavior is measurable

These may become stretch goals after the core kernel, driver, runtime, and test
story are working.
