# DDR 26: MMIO And Driver Framework Foundations

## Status

Accepted

## Context

Stage 4 begins the driver-framework and simulated-accelerator section. Earlier
milestones already provide S-mode execution, timer interrupts, preemptive
kernel threads, wait queues, a kernel heap, page allocation, Sv39 paging, and
safe usercopy. The next subsystem needs a disciplined way to describe devices,
access MMIO registers, and bind built-in drivers before adding accelerator
descriptors or interrupt-driven completion.

The current kernel targets QEMU `virt`, has no device-tree parser, and has no
general external interrupt/PLIC dispatch path yet. The kernel is single-hart
and uses static bounded data structures throughout.

## Decision

Add a minimal driver framework with typed MMIO helpers, immutable platform
resources, a bounded runtime device registry, and static built-in driver
probing.

MMIO helpers use typed volatile reads and writes. This prevents compiler-level
elision, merging, and reuse of register accesses. Hardware ordering is kept
explicit: the MMIO layer exposes low-level RISC-V fence helpers and semantic
wrappers for common driver boundaries. Drivers place fences where ordering
matters instead of hiding every ordering decision inside every register access.

Platform resources are immutable descriptions of hardware-like facts:

- name
- compatible string
- MMIO base and size
- IRQ number

The RISC-V platform layer implements the resource provider contract. The generic
driver framework builds mutable runtime `device_t` records from those resources.
This keeps platform facts separate from runtime binding state.

Built-in drivers live in a static table. `driver_probe_all()` scans drivers over
unbound compatible devices. One driver may bind multiple compatible devices.
The framework records the binding only after `probe()` returns success. Failed
probes leave devices unbound and are reported through the probe result rather
than treated as fatal by the framework.

PR 1 includes IRQ metadata and an `irq_handler_t` callback shape, but does not
implement external interrupt registration or dispatch. Interrupt-driven
accelerator completion remains a later Stage 4 milestone.

The PR 1 fake platform device owns static register storage exposed as an MMIO
resource. The fake driver is stateless and validates the device ID through
`mmio_read32()` during probe. The scenario validates lookup, binding, and
scratch-register read/write behavior through public device and MMIO APIs.

## Consequences

The framework is deterministic and allocation-free. It matches the current
kernel's static-table style while leaving room for future device-tree discovery
or dynamic registration if the platform grows.

The platform resource and runtime device split preserves a useful invariant:
the framework may bind drivers to resources, but it does not mutate the
platform's description of what hardware exists.

Keeping fences explicit has teaching value. Future descriptor submission code
will show the ordering boundary where normal memory must be visible before a
device observes a command kick, and where device completion must be observed
before the CPU consumes associated results.

The design does not yet prove real hardware cacheability or bus-ordering
behavior. The fake MMIO resource is normal static kernel memory, so the test
validates access discipline and framework separation, not device-memory
attributes. The current Sv39 mappings also do not encode rich device memory
types. That limitation is acceptable for QEMU `virt` and should be revisited if
the project targets a platform where cacheability attributes matter.

The first compatible lookup returns the first matching resource and is intended
as a singleton convenience. If Stage 4 later supports multiple accelerator
instances, the framework should add an iterator or indexed compatible lookup.

## Alternatives Considered

Putting fences inside every MMIO read/write would create a simple API contract,
but it would hide the meaningful ordering decisions from driver code and could
add unnecessary overhead. Relaxed and ordered helper variants were deferred as
more API surface than this stage needs.

Mutable platform `device_t` descriptors would be simpler, but they would mix
hardware facts with runtime binding state. A separate immutable resource table
better preserves ownership boundaries.

Runtime driver registration was deferred. There is no dynamic hardware
discovery yet, and static built-in drivers match the bounded style used by the
scheduler, wait queues, and allocator.

Full external interrupt routing was deferred. Adding PLIC-level behavior in PR
1 would make the foundation PR too large and would distract from MMIO and
binding invariants.

## Connections

ECE350's mechanism-versus-policy split applies directly. The framework answers
"what devices exist and which drivers can bind?" while scenarios and later
subsystems decide "is this device required and how should failures behave?"

The STM32 RTOS analogy is fixed peripheral resources plus driver code that owns
register interpretation. The analogy breaks around memory protection and
interrupt routing: this RISC-V kernel has Sv39 mappings and will eventually
need platform interrupt-controller handling, while the STM32 lab model often
uses a simpler MCU memory map and NVIC path.
