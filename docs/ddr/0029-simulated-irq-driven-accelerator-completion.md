# DDR 29: Simulated IRQ-Driven Accelerator Completion

## Status

Accepted

## Context

PR 3 added a descriptor ABI and `accel_submit_sync()` API, but completion was
still effectively polling/synchronous: the submit path advanced the platform
simulation and inspected final state. PR 4 needs the same public submit API to
complete through the driver ISR and wait queues.

The kernel has wait queues, interrupt masking, driver IRQ callback metadata,
and a simulated accelerator IRQ status register. It does not yet have PLIC
support or real external interrupt trap delivery.

## Decision

Add a split simulated IRQ path:

- `device_dispatch_irq(irq_t irq)` in the generic driver framework
- `platform_dispatch_pending_irqs()` in the RISC-V platform layer
- accelerator ISR completion in the accelerator driver

The generic dispatch function maps one IRQ number to one bound device handler
and returns whether a handler was called. It does not panic on unhandled IRQs.
The current platform validates unique non-`IRQ_NONE` IRQ resources during
`device_init()`, so shared IRQ semantics are deferred.

`platform_dispatch_pending_irqs()` checks simulated platform interrupt sources
and dispatches pending IRQs through the generic device mechanism. In this
controlled QEMU platform, a pending simulated IRQ with no bound handler is a
platform wiring bug, so the platform helper panics.

The accelerator driver owns one static request slot. `accel_submit_sync()`
validates the descriptor, claims the slot under `irq_save()`, writes
`ACCEL_CMD_BASE`, writes `START`, and sleeps on a driver-owned completion
predicate. It does not call `platform_accel_step()` for submission progress.

Public register-level helpers that can mutate accelerator lifecycle state,
including reset/start/control and IRQ acknowledgement, return `ACCEL_ERR_BUSY`
while that request slot is owned. This prevents a second thread from clearing
device state or IRQ bits out from under the blocked submitter. The ISR keeps a
private direct acknowledgement path for the IRQ it is currently handling.

The simulated hardware worker calls:

```c
platform_accel_step();
platform_dispatch_pending_irqs();
```

The ISR reads accelerator IRQ status, records request result from device and
descriptor status, sets the request completion predicate, acknowledges IRQ
bits, and wakes waiters. The ISR also tolerates spurious accelerator IRQs when
no request is active by acknowledging known IRQ bits and returning.

The submitter releases the request slot after wake, after it has read the
recorded result. Completion does not reset the device; callers still must reset
before the next command.

## Consequences

PR 4 proves interrupt-driven driver completion without pretending PLIC delivery
exists. A future real external interrupt path should be able to claim a hardware
IRQ and call `device_dispatch_irq()` without changing accelerator request
completion semantics.

The accelerator API remains stable: callers still use
`accel_submit_sync(cmd)`. The mechanism underneath changes from submit-side
simulation stepping to worker-driven simulated IRQ completion.

The single static request slot makes the API non-reentrant. Concurrent
submissions are rejected with `ACCEL_ERR_BUSY`; if the descriptor is safe, its
status becomes `ACCEL_CMD_STATUS_REJECTED`.

Register-level helper calls that would perturb an active descriptor request are
also rejected while the slot is owned. Callers may still read status registers,
but control/reset/ack policy is serialized behind the active request.

`irq_save()` is sufficient for the current single-hart kernel and matches the
existing wait-queue/mutex style. A future SMP kernel should replace this with a
spinlock or other interrupt-safe lock primitive.

## Alternatives Considered

Direct platform-to-driver callbacks were rejected because they collapse the
generic IRQ dispatch boundary.

A real PLIC/external interrupt path was deferred as too large for this PR. PR 4
is about driver completion semantics, not interrupt-controller bring-up.

Caller-provided request objects and fixed request tables were deferred. The
simulated accelerator has one command-base register and one in-flight command,
so a single driver-owned request slot is the simplest faithful model.

Resetting the device before `accel_submit_sync()` returns was rejected because
it hides the reset-before-start lifecycle from PR 2 and PR 3.

## Evidence

The `accelerator-irq-completion` scenario uses three kernel threads:

- a submitter that blocks in `accel_submit_sync()`
- a competing submitter that verifies the request slot is held before simulated
  IRQ delivery
- a simulator worker that advances the platform and dispatches pending IRQs

The scenario also verifies descriptor memory results, IRQ acknowledgement by
the ISR, raw-helper rejection while the request slot is owned,
reset-before-reuse behavior, and spurious IRQ acknowledgement.

The smoke marker is:

```text
milestone 20: interrupt-driven accelerator completion
```

## Connections

The ECE350 connection is condition-based sleeping. The wait queue is guarded by
a driver-owned `completed` predicate, and the check/sleep transition is
protected with interrupt masking to avoid lost wakeups.

The STM32 RTOS connection is an interrupt handler completing a peripheral
request and waking a blocked task. The analogy breaks at delivery: this PR uses
a simulated platform dispatcher, not the RISC-V PLIC or real external interrupt
traps.
