# Simulated Accelerator Design

## Scope

Stage 4 adds a small simulated accelerator to exercise driver concepts without
claiming to model a real GPU, NIC, storage controller, or NPU. The register
model milestone introduced a hardware-like MMIO device with deterministic
status, control, and interrupt-status behavior. The descriptor milestone adds a
kernel submission API and one command operation over allocator-managed physical
memory. The IRQ-completion milestone routes descriptor completion through the
driver ISR and wait queues.

This stage still does not implement real external interrupt-controller
delivery, userspace syscalls, timeout policy, or throughput measurements. Those
belong to later Stage 4 and Stage 5 work.

## Register Layout

The accelerator exposes these registers:

```text
0x00 ACCEL_ID          read-only identity value
0x04 ACCEL_STATUS      current device state
0x08 ACCEL_CONTROL     consume-on-step command register
0x0c ACCEL_IRQ_STATUS  pending notification bits
0x10 ACCEL_IRQ_ACK     write-1-to-clear notification bits
0x18 ACCEL_CMD_BASE    physical address of a command descriptor
```

`ACCEL_CMD_BASE` is a 64-bit register and is aligned at offset `0x18`.
`RESET` clears it to zero. A raw `START` with `ACCEL_CMD_BASE == 0` remains a
register-level self-test path for the earlier milestone. The descriptor
submission API always writes a nonzero validated descriptor address before
starting the device.

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

The simulated device itself completes a started operation when
`platform_accel_step()` advances it:

```text
START from IDLE:
  IDLE -> BUSY -> DONE
  IRQ_STATUS = DONE
```

Earlier milestones called the step hook from register helpers. Descriptor
submission no longer steps the device from `accel_submit_sync()`: a separate
simulated hardware context must advance the device and dispatch pending IRQs.
Later milestones may replace the explicit worker-driven step with timer-driven
or real external interrupt progress while preserving the register vocabulary.

Descriptor submission uses the same lifecycle:

```text
RESET -> IDLE
driver validates descriptor and buffer
driver writes ACCEL_CMD_BASE
driver writes START
separate simulator context executes one step
platform dispatch invokes driver ISR
DONE or ERROR
RESET required before the next command
```

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
`kernel/arch/riscv64/accel_platform.c`. Register-level helper paths still call
`platform_accel_step()` after raw control or ack writes to model hardware
reacting to the consumed register. The descriptor submit path writes `START`
and blocks; simulator code advances hardware separately.

This hook is an explicit simulation compromise. The fake hardware is backed by
ordinary static kernel memory, not an independent QEMU bus device that can react
automatically to stores. Keeping the hook in the platform layer preserves the
boundary that platform code owns hardware-like state, while the accelerator
driver owns register interpretation and kernel-facing operations.

For descriptor work, the driver fences ordinary descriptor writes before
writing MMIO registers. This preserves the invariant that descriptor contents
are published before the device is started.

## Interrupt-Driven Completion

`accel_submit_sync()` now uses interrupt-driven completion internally while
preserving the public contract that it returns after the command is complete.
The driver owns one static in-flight request slot:

```text
descriptor pointer
completion wait queue
in-use flag
completed predicate
result code
```

The slot is protected with `irq_save()` critical sections, matching the
single-hart scheduler and wait-queue style already used by mutexes. The
submitter claims the slot before ringing the device doorbell and releases it
only after waking and reading the result. A second submitter while the slot is
owned receives `ACCEL_ERR_BUSY` and descriptor status `REJECTED`.

The wait condition is driver-owned request state, not the device-owned
descriptor status. The ISR bridges the two:

```text
read IRQ_STATUS
if no active request, ack known bits and return
read device status and descriptor status
record request result
set completed predicate
ack IRQ bits
wake waiters
```

Acknowledgement happens in the ISR so stale IRQ bits do not poison the next
submit. The ISR does not reset the device. After completion, the device remains
in `DONE` or `ERROR`, and callers must reset before submitting again.

## Driver API

The accelerator driver exposes a small register-level API:

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

Descriptor submission lives in `kernel/drivers/accel_cmd.h`:

```c
int accel_submit_sync(accel_cmd_t *cmd);
```

The current descriptor format is:

```c
typedef struct accel_cmd {
    uint32_t op;
    uint32_t flags;
    uint64_t dst_pa;
    uint32_t len;
    uint32_t value;
    uint32_t status;
    uint32_t reserved;
} accel_cmd_t;
```

PR 3 supports only `ACCEL_CMD_OP_MEMSET`. The operation writes
`(uint8_t)value` into the destination range, matching C `memset` behavior where
only the low 8 bits are consumed.

`accel_submit_sync()` requires:

- descriptor pointer is non-null, 8-byte aligned, and fully contained in one
  allocator-managed page
- operation is `MEMSET`
- `flags` and `reserved` are zero
- destination range is nonempty and fully contained in one allocator-managed
  page
- device status is `IDLE`
- IRQ status is zero
- no other accelerator request is in flight

The page allocator helper proves that a physical byte range sits inside the
allocator-managed RAM window and does not cross a page boundary. It does not
prove exclusive caller ownership of that frame. For now, kernel callers and
scenarios must use allocated pages they own. Real frame ownership or DMA
pinning is deferred.

The driver validates at the kernel API boundary. The simulated platform also
defensively validates command descriptors before execution. This prevents an
invalid command from silently writing memory if a future driver bug bypasses
the intended API checks.

Descriptor status reports command result:

```text
PENDING   accepted by driver, visible to device
OK        execution completed
INVALID   descriptor encoding or memory range was invalid
ERROR     execution began but failed
REJECTED  safe descriptor, but device lifecycle rejected submission
```

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

The `accelerator-descriptors` scenario verifies:

- allocator-backed descriptor and destination pages
- page-contained but not page-aligned destination range
- `MEMSET` writes only the requested byte range
- low 8 bits of `value` are used
- invalid operation is rejected before device start
- page-crossing destination range is rejected
- unmanaged, unaligned, and page-crossing descriptor locations are rejected
- second submit without reset is rejected with descriptor status `REJECTED`
- descriptor submission still succeeds when a separate simulator worker
  advances the device and dispatches the pending IRQ

The scenario prints:

```text
scenario: accelerator-descriptors
accel: descriptor memset passed
accel: descriptor validation passed
accel: descriptor lifecycle rejection passed
milestone 19: accelerator descriptors
```

The `accelerator-irq-completion` scenario verifies:

- submitter holds the driver request slot before simulated IRQ delivery
- competing submitter is rejected while the slot is owned
- simulator worker explicitly calls `platform_accel_step()` and
  `platform_dispatch_pending_irqs()`
- driver ISR records completion, acks IRQ status, and wakes the blocked
  submitter
- completion leaves the device in `DONE` with IRQ status cleared
- reset is still required before descriptor reuse
- spurious raw accelerator IRQ with no active request is acked and ignored

The scenario prints:

```text
scenario: accelerator-irq-completion
accel: submitter blocked before irq
accel: competing submit rejected
accel: irq completion woke submitter
accel: reset allows descriptor reuse
accel: spurious irq ack passed
milestone 20: interrupt-driven accelerator completion
```

## Course Connection

The ECE350 connection is the distinction between a condition and a notification.
`STATUS_DONE` records the device condition, while `IRQ_STATUS_DONE` records that
software has a pending notification to acknowledge. Clearing the notification
does not consume or reset the condition.

The wait-queue connection is the lost-wakeup invariant: the submitter checks
the driver-owned `completed` predicate and sleeps while interrupts are masked,
so the ISR cannot complete and wake between the check and wait enqueue on this
single-hart kernel.

The STM32 RTOS analogy is a peripheral driver wrapping register protocols behind
meaningful operations, then using an interrupt handler to complete a blocked
caller. The analogy breaks because this device is simulated in kernel memory
and uses an explicit worker-driven step hook; a real MCU peripheral or QEMU
device would react independently to bus writes.
