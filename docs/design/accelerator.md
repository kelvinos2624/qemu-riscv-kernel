# Simulated Accelerator Design

## Scope

Stage 4 adds a small simulated accelerator to exercise driver concepts without
claiming to model a real GPU, NIC, storage controller, or NPU. The register
model milestone introduces a hardware-like MMIO device with deterministic
status, control, and interrupt-status behavior.

This milestone does not implement command descriptors, payload buffers,
blocking completion, real external interrupt dispatch, userspace syscalls, or
throughput measurements. Those belong to later Stage 4 and Stage 5 work.

## Register Layout

The accelerator exposes these PR 2 registers:

```text
0x00 ACCEL_ID          read-only identity value
0x04 ACCEL_STATUS      current device state
0x08 ACCEL_CONTROL     consume-on-step command register
0x0c ACCEL_IRQ_STATUS  pending notification bits
0x10 ACCEL_IRQ_ACK     write-1-to-clear notification bits
```

Descriptor-related registers are intentionally not implemented yet. Future
descriptor work should extend the layout after `ACCEL_IRQ_ACK` rather than
assigning placeholder semantics before descriptors exist.

## State Machine

The status register uses one state bit at a time:

```text
IDLE
BUSY
DONE
ERROR
```

`RESET` is the only command that returns the device to `IDLE`. IRQ
acknowledgement clears notification bits only; it does not reset operation
state.

The PR 2 model completes synchronously:

```text
START from IDLE:
  IDLE -> BUSY -> DONE
  IRQ_STATUS = DONE
```

This synchronous completion validates register semantics without introducing
delayed completion, wait queues, or interrupt dispatch yet. Later milestones may
replace the immediate completion policy with delayed or interrupt-driven
progress while preserving the register vocabulary.

Invalid start transitions enter the error state:

```text
START from BUSY/DONE/ERROR:
  STATUS = ERROR
  IRQ_STATUS = ERROR
```

Error is dominant in this simple model. Once `ERROR` is entered, only `RESET`
restores `IDLE`.

If `RESET` and `START` are written together, `RESET` has priority, `START` is
ignored, `STATUS` becomes `IDLE`, `IRQ_STATUS` clears, and `CONTROL` is
consumed.

## Platform Simulation Hook

The accelerator backing registers live in
`kernel/arch/riscv64/accel_platform.c`. The driver writes MMIO registers through
the shared MMIO helpers, then calls `platform_accel_step()` to model the
hardware reacting to the consumed control or ack register.

This hook is an explicit simulation compromise. The fake hardware is backed by
ordinary static kernel memory, not an independent QEMU bus device that can react
automatically to stores. Keeping the hook in the platform layer preserves the
boundary that platform code owns hardware-like state, while the accelerator
driver owns register interpretation and kernel-facing operations.

## Driver API

The PR 2 accelerator driver exposes a small register-level API:

```c
int accel_reset(void);
int accel_start_selftest(void);
int accel_get_status(uint32_t *status_out);
int accel_get_irq_status(uint32_t *irq_status_out);
int accel_ack_irq(uint32_t mask);
int accel_write_control_raw(uint32_t control);
```

Each call looks up the first compatible bound accelerator device. This keeps PR
2 stateless and reinforces that driver availability comes from the device
framework. It also means the current API models a singleton accelerator.
Multi-instance enumeration or handles are deferred.

## Test Evidence

The `accel-registers` scenario verifies:

- reset clears status to `IDLE` and clears IRQ status
- start from `IDLE` completes synchronously to `DONE`
- done acknowledgement clears only the IRQ bit
- start after `DONE` enters `ERROR`
- error acknowledgement clears only the IRQ bit
- combined reset/start gives reset priority and ignores start
- unknown control bits are rejected by the driver API

The scenario prints:

```text
scenario: accel-registers
accel: reset idle
accel: start done
accel: invalid transition error
accel: reset priority passed
milestone 18: simulated accelerator registers
```

## Course Connection

The ECE350 connection is the distinction between a condition and a notification.
`STATUS_DONE` records the device condition, while `IRQ_STATUS_DONE` records that
software has a pending notification to acknowledge. Clearing the notification
does not consume or reset the condition.

The STM32 RTOS analogy is a peripheral driver wrapping register protocols behind
meaningful operations. The analogy breaks because this device is simulated in
kernel memory and uses an explicit step hook; a real MCU peripheral or QEMU
device would react independently to bus writes.
