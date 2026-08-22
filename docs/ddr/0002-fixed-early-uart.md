# DDR 2: Use a Fixed Early UART Address

## Status

Accepted

## Context

The kernel needs visible output before the driver framework, device discovery,
interrupt handling, or allocation exist.

## Decision

Use the QEMU `virt` 16550 UART MMIO base address `0x10000000` for early console
output.

## Alternatives Considered

- Parse the flattened device tree during early boot.
- Introduce a static device table immediately.
- Defer console output until the driver framework exists.

## Consequences

The early console is simple and deterministic, which is useful for boot testing.
The tradeoff is that the first console driver is platform-specific. A later
driver framework should absorb this constant behind a device object or discovery
path.

## Evidence

The UART base is defined in `kernel/drivers/uart.h` and used by
`kernel/drivers/uart.c`.
