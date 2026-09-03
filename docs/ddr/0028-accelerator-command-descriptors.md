# DDR 28: Accelerator Command Descriptors

## Status

Accepted

## Context

PR 1 added MMIO and driver registration foundations. PR 2 added a simulated
accelerator register model with reset, start, status, and IRQ-status behavior.
PR 3 needs to move from register-only commands to descriptor-based work
submission while preserving the driver/device boundary.

The kernel still has one active identity-mapped address space, no real DMA
ownership tracking, no userspace accelerator API, no external interrupt
dispatch path, and no timeout policy. The descriptor milestone should validate
safe submission and execution without pulling those later milestones forward.

## Decision

Add a public command descriptor ABI in `kernel/drivers/accel_cmd.h` and expose:

```c
int accel_submit_sync(accel_cmd_t *cmd);
```

The descriptor is 32 bytes and 8-byte aligned:

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

PR 3 supports only `ACCEL_CMD_OP_MEMSET`. The operation writes the low 8 bits of
`value` into the destination range.

Extend the accelerator register layout with one 64-bit command-base register:

```text
0x18 ACCEL_CMD_BASE
```

The driver writes the physical address of a validated descriptor to
`ACCEL_CMD_BASE`, fences ordinary memory before MMIO doorbell writes, writes
`START`, invokes the platform simulation step once, and then inspects final
status. The synchronous single-step completion is intentionally scoped to PR 3;
interrupt-driven completion belongs to PR 4.

`accel_submit_sync()` requires the device to already be clean and idle:

```text
STATUS == IDLE
IRQ_STATUS == 0
```

It does not reset or acknowledge IRQ bits for the caller. After `DONE` or
`ERROR`, callers must acknowledge any pending IRQ status and reset the device
before the next submission.

Validation is hybrid:

- the driver validates descriptor pointer safety, command encoding, buffer
  range, and lifecycle readiness before submission
- the simulated platform defensively validates executable descriptor contents
  before mutating memory

The page allocator exposes a narrow helper for page-contained managed physical
ranges. It proves that a range is nonempty, lies in the allocator-managed RAM
window, and does not cross a page boundary. It does not prove exclusive frame
ownership by the caller.

Descriptor status distinguishes command result from device lifecycle:

```text
PENDING   accepted by driver, visible to device
OK        execution completed
INVALID   descriptor encoding or memory range was invalid
ERROR     execution began but failed
REJECTED  safe descriptor, but device lifecycle rejected submission
```

If the descriptor pointer itself is unsafe, the driver returns
`ACCEL_ERR_INVALID` without touching it. If the descriptor is safe but invalid,
the driver writes `ACCEL_CMD_STATUS_INVALID`. If the device is not clean and
idle, the driver writes `ACCEL_CMD_STATUS_REJECTED` and returns
`ACCEL_ERR_BUSY`.

## Consequences

PR 3 demonstrates descriptor ownership and physical-buffer validation without
creating userspace or interrupt semantics prematurely.

Keeping execution in the platform simulation preserves the hardware model: the
driver prepares and submits work, while the simulated device performs the
operation and updates status.

The clean-idle precondition is stricter than many production drivers, which may
ack stale bits or reset internally. The stricter policy is intentional teaching
surface: tests and callers must understand the lifecycle rather than receiving
hidden cleanup.

The current memory validation is not a DMA ownership model. A future PR that
accepts userspace buffers or concurrent kernel owners must add frame ownership,
pinning, mapping, or copy-in/copy-out policy rather than relying only on the
managed page-contained helper.

## Alternatives Considered

Supporting checksum or XOR operations was deferred. `MEMSET` is enough to prove
descriptor submission, memory mutation, and result validation.

Requiring buffers to be page-aligned would simplify validation but make the API
less flexible. Allowing page-contained offsets still prevents cross-page
complexity.

Using low/high 32-bit command-base registers would mirror many devices and
teach multi-register ordering hazards, but it would distract from the current
descriptor milestone on this RV64 kernel.

Letting `accel_submit_sync()` reset automatically would be more ergonomic but
would hide the reset-before-start rule established by the register model.

## Evidence

The `accelerator-descriptors` scenario allocates descriptor and buffer pages
from the physical page allocator, submits a valid page-contained `MEMSET`
command, verifies the memory result, rejects invalid command and range cases,
and verifies lifecycle rejection for a second submit without reset.

The smoke marker is:

```text
milestone 19: accelerator descriptors
```

## Connections

The ECE350 connection is syscall-style boundary validation. A caller provides a
pointer-shaped argument, the kernel subsystem validates that the object and its
referenced memory are safe for the operation, and only then crosses into the
lower-level mechanism.

The STM32 RTOS connection is descriptor-like peripheral programming: software
publishes a memory object, orders the writes, and then rings a device doorbell.
The analogy breaks because this accelerator is still a synchronous in-kernel
simulation hook rather than independent hardware progressing in parallel.
