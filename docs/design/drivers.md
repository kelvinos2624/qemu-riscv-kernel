# Driver Framework Design

## Scope

Stage 4 introduces the first kernel-side driver framework. This stage moves the
project from a kernel that manages only itself to a kernel that mediates
hardware-like devices.

The first PR provides MMIO helpers, immutable platform resource descriptions, a
bounded runtime device registry, built-in driver probing, and descriptive IRQ
metadata. It does not yet implement external interrupt routing, blocking driver
completion, user-facing device syscalls, or a full simulated accelerator.

## MMIO Access

MMIO registers are privileged side-effect locations, not ordinary C memory.
The driver layer exposes typed volatile helpers:

```c
uint8_t mmio_read8(uintptr_t addr);
uint16_t mmio_read16(uintptr_t addr);
uint32_t mmio_read32(uintptr_t addr);
uint64_t mmio_read64(uintptr_t addr);

void mmio_write8(uintptr_t addr, uint8_t value);
void mmio_write16(uintptr_t addr, uint16_t value);
void mmio_write32(uintptr_t addr, uint32_t value);
void mmio_write64(uintptr_t addr, uint64_t value);
```

The `volatile` access prevents the compiler from treating register reads and
writes as ordinary cacheable values that can be eliminated, merged, or reused
across source-level accesses.

The MMIO layer also provides explicit fence helpers:

```c
void riscv_fence_rw_w(void);
void riscv_fence_r_rw(void);
void mmio_fence_before_device_write(void);
void mmio_fence_after_device_read(void);
```

The low-level helpers expose the RISC-V fence shape. The semantic helpers name
common driver intent. Drivers decide where ordering is required, such as before
kicking a descriptor-based command or after observing device completion.

The current QEMU `virt` kernel maps MMIO through ordinary Sv39 PTE permissions.
It does not yet model richer device-memory cacheability attributes. That is a
platform limitation to revisit if the kernel moves beyond this simple QEMU
environment.

## Device Resources

Platform resources describe hardware-like facts:

- instance name
- compatible string
- MMIO base
- MMIO size
- IRQ number or `IRQ_NONE`

The resource table is immutable. On the current target,
`kernel/arch/riscv64/platform.c` provides the platform resource table through
the generic `drivers/platform.h` contract. This keeps RISC-V/QEMU platform facts
out of the generic driver framework.

## Runtime Devices

The framework builds a bounded runtime registry from the immutable platform
resources during `device_init()`. Runtime `device_t` records point at resource
descriptions and hold binding state.

This split preserves the invariant that hardware-like resources are platform
facts, while driver binding is runtime kernel state.

## Driver Binding

Built-in drivers are compiled into a static table. `driver_probe_all()` scans
built-in drivers over unbound runtime devices and matches by compatible string.
One driver may bind multiple compatible device instances.

Binding happens only after probe success:

1. the framework finds an unbound compatible device
2. the driver's `probe()` validates the resource
3. the framework records the binding only if `probe()` returns success

A failed probe leaves the device present but unbound. The framework reports
probe counts, but boot policy does not panic on failed or unmatched devices.
Scenarios or future required-driver code decide which devices must exist.

The first compatible lookup is intentionally a convenience API for singleton
devices. Multi-instance users should prefer exact name lookup until an iterator
or indexed compatible lookup exists.

## IRQ Metadata

PR 1 defines `irq_t`, `IRQ_NONE`, and `irq_handler_t`, and drivers may declare
an IRQ handler callback. No external interrupt registration or dispatch path
invokes those callbacks yet.

This keeps the descriptor shape ready for later accelerator completion work
without pretending that PLIC/external interrupt routing is implemented.

## Boot Integration

Driver framework initialization runs after `heap_init()` and before scenario
execution:

```text
heap_init
device_init
driver_probe_all
scenario_run
```

This lets scenarios observe a stable post-probe device registry. The PR 1 fake
driver uses static state only, but later drivers may rely on the heap if their
probe path needs dynamic kernel-owned objects.

## Test Evidence

The `driver-framework` scenario validates observable behavior:

- fake platform device lookup by exact name
- fake platform device lookup by compatible string
- successful driver binding after boot-time probing
- fake driver probe validates a device ID register through MMIO helpers
- scenario writes and reads a scratch register through MMIO helpers
- IRQ metadata and callback shape exist but are not dispatched

The scenario prints:

```text
scenario: driver-framework
driver: fake device bound
driver: mmio read/write passed
milestone 17: driver framework
```

## Course Connection

The ECE350 connection is the separation between mechanism and policy. The
framework provides bounded device lookup and binding mechanics, while scenarios
or later subsystems decide whether a missing device is fatal. This mirrors the
course distinction between a scheduler mechanism and the policy that chooses
which task should run.

The STM32 RTOS connection is the fixed platform resource model. MCU peripheral
base addresses are platform facts, while drivers interpret registers and expose
kernel services. The analogy breaks at interrupt routing: STM32 NVIC setup is
not the same as RISC-V external interrupt and PLIC delivery, so Stage 4 defers
real external IRQ dispatch until the accelerator completion milestone.
