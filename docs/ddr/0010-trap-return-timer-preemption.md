# DDR 10: Use Trap-Return Switching for Timer Preemption

## Status

Accepted

Follow-up: DDR 21 migrates normal kernel execution to S-mode. Trap-frame-based
switching remains the scheduler model, but kernel thread control traps now use
fixed-width `ebreak` delegated to S-mode, and the restore path exits with
`sret`.

## Context

The kernel already had a machine timer interrupt and a FIFO ready queue.
Cooperative scheduling was still entered explicitly by threads, while timer
interrupts only updated the monotonic tick counter.

Calling `thread_yield()` directly from the timer interrupt would mix two
different context models. A cooperative yield is a normal C call, but a timer
interrupt can arrive between arbitrary instructions after the trap entry has
already saved a full machine context.

## Decision

Use trap-frame-based scheduling for both machine-mode thread `ecall`s and timer
preemption.

The timer interrupt handler remains lean:

- increment the kernel tick counter
- program the next `mtimecmp`
- notify the scheduler tick hook

The trap handler then decides whether to return the same trap frame or a
different thread's saved trap frame. The assembly restore path uses the frame
selected by C and exits with `mret`.

The timer tick is 1 ms on the current QEMU `virt` 10 MHz timebase assumption.
`THREAD_QUANTUM_TICKS` is 10, giving an approximate 10 ms scheduler quantum.

## Alternatives Considered

- Call `thread_yield()` from the timer interrupt handler.
- Run ready-queue scheduling directly inside the timer driver.
- Keep cooperative switching on `context_switch()` and add a separate
  interrupt-only switch mechanism.

## Consequences

The timer critical path stays short and deterministic. Scheduler policy remains
in `thread.c`, hardware timer reprogramming remains in `timer.c`, and register
restore mechanics remain in `trap.S`.

Cooperative `thread_yield()` and `thread_exit()` now enter the trap path through
machine-mode `ecall`s, so cooperative and preemptive scheduling share one saved
context format.

The tradeoff is that the scheduler now depends more directly on trap-frame
layout and the trap restore path. This is acceptable at this stage because all
threads are still machine-mode kernel threads.

This mirrors the ECE350/STM32 RTOS split between SysTick and PendSV: the timer
records that scheduler work is needed, and the actual switch happens at a
controlled exception-return boundary. RISC-V machine mode does not provide
PendSV, so this kernel uses the trap return path as the equivalent boundary.

## Evidence

The QEMU smoke test starts a CPU-bound thread that does not call
`thread_yield()`. It then verifies that a peer thread runs before the CPU-bound
thread finishes, which proves the timer can recover CPU ownership
preemptively.
