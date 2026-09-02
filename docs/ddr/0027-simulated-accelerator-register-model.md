# DDR 27: Simulated Accelerator Register Model

## Status

Accepted

## Context

PR 1 added the driver framework, MMIO helpers, immutable platform resources,
and built-in driver probing. PR 2 needs the first real Stage 4 device so later
milestones can add command descriptors, interrupt-driven completion, timeout
handling, and userspace runtime APIs.

The accelerator is intentionally simulated inside the kernel for now. The
project does not yet add a custom QEMU device or a general external interrupt
path. This makes it important to preserve the driver/device boundary even though
the backing register storage is ordinary static memory.

## Decision

Replace the temporary PR 1 fake device with a simulated accelerator resource and
driver.

The accelerator backing register storage lives in
`kernel/arch/riscv64/accel_platform.c`, while the generic platform resource
enumeration remains in `kernel/arch/riscv64/platform.c`. The accelerator driver
lives under `kernel/drivers/` and binds by the compatible string
`qemu-rtos,sim-accel`.

PR 2 implements only registers with testable behavior:

```text
ACCEL_ID
ACCEL_STATUS
ACCEL_CONTROL
ACCEL_IRQ_STATUS
ACCEL_IRQ_ACK
```

Descriptor registers are reserved for future design but are not implemented in
code. This avoids placeholder behavior and keeps the milestone focused.

`ACCEL_CONTROL` is a consume-on-step command register. After the driver writes a
control value through MMIO, it calls `platform_accel_step()` to model hardware
consuming the command and updating status/IRQ registers. This hook is a
deliberate simulation compromise because static kernel memory cannot react to
stores like an independent bus device.

`START` from `IDLE` completes synchronously in PR 2:

```text
IDLE -> BUSY -> DONE
IRQ_STATUS = DONE
```

This validates the register contract without introducing asynchronous
completion yet. `START` from `BUSY`, `DONE`, or `ERROR` enters `ERROR`, and
`IRQ_STATUS` becomes `ERROR`. Error is dominant until reset.

IRQ acknowledgement clears pending IRQ bits only. It does not change operation
state. Only `RESET` returns the accelerator to `IDLE`.

If `RESET` and `START` are written together, reset has priority and start is
ignored. The device becomes `IDLE`, IRQ status is cleared, and the control value
is consumed.

The accelerator driver exposes a small kernel-facing register API and looks up
the first compatible bound accelerator device on each call rather than caching a
global pointer.

## Consequences

The driver framework is now validated against a real project device instead of
a permanent artificial test device. The `driver-framework` scenario continues
to prove binding and MMIO discipline through the accelerator.

The synchronous completion behavior is intentionally not the final accelerator
contract. Later descriptor and interrupt milestones should preserve the register
vocabulary while replacing immediate completion with delayed completion,
wait-queue integration, and interrupt dispatch.

Looking up the device on each API call keeps PR 2 stateless and simple. It is
O(number of platform devices), which is acceptable for the current bounded
registry. A future hot path or multi-instance accelerator should introduce
handles, cached binding state, or iterator-style lookup.

The platform simulation hook is a backdoor only in a controlled sense: it models
device-side reaction to MMIO writes. Driver and scenario code should not mutate
accelerator backing storage directly.

## Alternatives Considered

Implementing descriptor registers now was deferred to avoid scope creep. Every
implemented PR 2 register should have behavior that can be tested in PR 2.

Letting the driver update all status and IRQ registers directly would be simpler
but would collapse hardware state into driver state. Keeping the state machine
in the platform simulation layer better preserves the device boundary.

Timer-driven or interrupt-driven completion was deferred. Those behaviors are
important, but they belong to later milestones once the register contract is
stable.

Acknowledging completion could have returned the device to `IDLE`, but that
would mix notification handling with operation-state reset. Keeping ack and
reset separate prepares the model for later completion ownership.

## Connections

ECE350's event/wakeup distinction maps to `STATUS` versus `IRQ_STATUS`.
Acknowledging a notification is not the same as resetting the condition that
made the notification true.

The STM32 RTOS connection is driver-owned register interpretation over
platform-owned peripheral resources. The analogy breaks because this simulated
accelerator uses a platform step hook, while real peripherals progress
independently of CPU function calls.
