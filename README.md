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

Progress: `[##########----------] 50%`

The kernel currently boots on QEMU `virt`, initializes UART output, installs a
machine-mode trap path, handles timer interrupts, and runs preemptive FIFO
round-robin kernel threads using trap-frame-based switching. It also supports
tick-based `thread_sleep()` through an absolute-deadline sleep queue and
event-style blocking through wait queues. It now includes non-recursive kernel
mutexes with FIFO owner transfer and timeout-aware blocking. Memory management,
userspace isolation, syscalls, and the simulated accelerator driver remain
future milestones.

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
- Initial privilege mode: machine mode
- ISA baseline: `rv64imac_zicsr`
- Firmware: none, using QEMU `-bios none`
- Kernel load address: `0x80000000`
- Console: QEMU `virt` 16550 UART at `0x10000000`

Machine mode is used for the first milestone to keep the earliest boot path
fully owned by the kernel. Supervisor mode, SBI integration, page tables, and
userspace isolation are later milestones.

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

- direct machine-mode trap vector
- full trap frame for general-purpose registers and machine CSRs
- C trap handler
- controlled `ecall` self-test for trap return

Timer interrupt setup is complete:

- QEMU `virt` machine timer MMIO
- `mtimecmp` programming
- 1 ms kernel tick on the QEMU `virt` 10 MHz timebase
- `mie.MTIE` and `mstatus.MIE` enablement
- machine-timer interrupt dispatch through the trap handler
- monotonic kernel tick counter

Kernel thread scheduling setup is complete:

- `thread_init()` / `thread_start()` lifecycle
- reserved TID 0 null task
- static per-thread kernel stacks
- bounded FIFO ready queue over real thread IDs
- machine-mode `ecall` path for `thread_yield()` and `thread_exit()`
- timer-driven preemptive FIFO round-robin
- trap-frame-based thread switching on trap return
- queue membership tracking to support timeout-aware blocking
- `thread_sleep(ticks)` with a sorted absolute-deadline sleep queue
- FIFO wait queues for event-style blocking and wakeups
- non-recursive kernel mutexes built on wait queues
- timeout-aware waits and `mutex_lock_timeout()`
- `thread_exit()` for retiring finished threads
- 10 tick / 10 ms scheduler quantum
- interrupt masking around scheduler state

Scheduler tracing and priority-inversion experiments are the next scheduling
milestones.

Expected boot output:

```text
qemu-rtos: booting RISC-V kernel
kernel_start=0x0000000080000000 kernel_end=... stack_top=...
milestone 1: boot, stack, bss, uart console
trap: mtvec=...
trap: self-test passed
milestone 2: trap vector setup
timer: interval=...
timer: observed ... ticks
milestone 2: timer interrupt setup
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
milestone 9: timed waits
```

## Planned Architecture

The system will grow through five major deliverables.

1. Kernel foundations
   Boot, stack setup, UART, panic handling, trap vector setup, timer interrupt
   setup, and basic kernel thread state.

2. Scheduling and synchronization
   Preemptive round-robin scheduling, sleep queues, wait queues, mutexes,
   timeout-aware blocking wakeups, and eventually priority-inversion
   experiments.

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

A later milestone will add a small simulated hardware accelerator rather than a
fake GPU, TPU, NIC, or storage controller. The device will expose MMIO registers
and accept command descriptors for operations such as memory set, XOR, or
checksum.

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
|   `-- drivers/
|-- tools/
`-- user/
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

Run it in QEMU:

```sh
make run
```

Run the current QEMU smoke test:

```sh
make test
```

Useful targets:

```sh
make run
make debug
make test
make clean
make toolcheck
```

## Testing Strategy

The project will favor repeatable tests over manual observation. The first test
is a black-box QEMU integration test that validates the latest milestone through
the kernel's UART output. It currently checks timeout-aware mutex blocking
without requiring an in-kernel unit-test framework. The current test verifies
that one thread times out while waiting for a mutex, the idle task runs while
all real threads are blocked, and a later thread can still acquire the mutex
after the owner unlocks.

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
