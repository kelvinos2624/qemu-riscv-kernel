# DDR 30: Accelerator Timeout And Error Handling

## Status

Accepted

## Context

PR 4 made accelerator descriptor completion interrupt-driven. A submitter owns
one driver request slot, sleeps on a wait queue, and wakes when the ISR records
completion. That still left one important driver failure mode undefined: what
happens if simulated hardware never completes the request.

The kernel already has timeout-aware wait queues. The accelerator still has a
single command-base register, one in-flight command, no cancellation register,
and no real independent hardware execution context.

## Decision

Add a timeout-aware API:

```c
int accel_submit_sync_timeout(accel_cmd_t *cmd, uint64_t ticks);
```

Keep `accel_submit_sync(cmd)` as the indefinite-wait API by routing it through
the timeout-aware implementation with an effectively infinite timeout.

Use these timeout semantics:

- `ticks == 0` times out immediately before device submission
- a safe descriptor receives `ACCEL_CMD_STATUS_TIMEOUT`
- the API returns `ACCEL_ERR_TIMEOUT`
- completion wins if the ISR wake is observed before the timeout return
- timeout wins if `wait_queue_sleep_timeout()` returns `WAIT_TIMEOUT`

After a nonzero timeout, the driver releases the software request slot so the
blocked thread can return, but it records a driver-owned recovery flag. Public
submits, raw starts, and public IRQ acknowledgements return `ACCEL_ERR_BUSY`
until `accel_reset()` succeeds. Reset is allowed after timeout and clears the
recovery flag.

Late accelerator IRQs after timeout are treated as spurious because no active
request owns the slot. The ISR acknowledges known IRQ bits and returns. If a
late simulator step reaches a descriptor already marked `TIMEOUT`, the platform
simulator enters device error state without rewriting that descriptor status.

## Consequences

The driver now has an explicit recovery invariant:

```text
timeout observed -> public lifecycle operations blocked -> reset -> idle/reusable
```

This avoids silently accepting a second command while stale simulated hardware
state may still exist. It also keeps the descriptor result stable for the caller
that observed a timeout.

The implementation deliberately does not claim real hardware cancellation. On a
real DMA-capable device, timeout recovery would need reset quiescence, DMA
ownership rules, and a policy for buffers that hardware may still touch. This
PR documents that reset/cancel sophistication is deferred.

The policy is driver-owned. Wait queues provide the blocking mechanism, the
platform simulator provides hardware-like state transitions, and the driver
decides when the device is reusable after timeout.

## Alternatives Considered

Replacing `accel_submit_sync()` with a timeout-only API was rejected because it
would force existing callers to choose a timeout even when indefinite blocking
is the intended contract.

Returning busy for zero-tick timeout was rejected because zero ticks should
exercise the timeout path itself, not the device lifecycle path.

Keeping the request slot permanently owned after timeout was rejected because it
would strand the caller and make recovery harder to test. Releasing the slot but
requiring reset preserves forward progress while still blocking unsafe reuse.

Silently accepting another submit after timeout was rejected because the
simulated device can still contain a pending `START` and command pointer.

Resetting automatically inside the timeout path was rejected because it hides an
important driver policy decision from callers and overstates what this simulated
device can guarantee.

## Evidence

The `accelerator-timeout-error-handling` scenario verifies:

- zero-tick timeout returns before device start
- a request times out when the simulator worker does not advance hardware
- public lifecycle operations are rejected until reset after timeout
- a late simulated IRQ is acknowledged after the timeout
- the timed-out descriptor remains `TIMEOUT` after late IRQ cleanup
- reset restores the device to reusable state
- a later timed request completes successfully before its timeout
- invalid commands through the timed API are rejected before device start

The smoke marker is:

```text
milestone 21: accelerator timeout/error handling
```

## Connections

The ECE350 connection is the race between event wake and timeout wake. The wait
queue provides the mechanism for sleeping on both an event and a deadline; the
driver owns the policy for what the resource means after timeout.

The STM32 RTOS connection is a peripheral request with a timeout and explicit
reset recovery. The analogy breaks at hardware cancellation: this simulated
accelerator has no bus-master DMA or reset-completion handshake, so the PR
models only the software-side recovery contract.
