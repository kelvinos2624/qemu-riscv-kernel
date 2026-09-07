# DDR 38: Runtime Tracing for Stage 5 Boundaries

## Status

Accepted

## Context

The kernel already had a fixed in-memory trace ring for scheduler and
synchronization events. Stage 5 added scheduled userspace, syscall stubs,
task-aware usercopy, and a userspace accelerator syscall. The next tracing
milestone needs to show the path from a U-mode runtime call through syscall
dispatch and, for accelerator work, into the driver request lifecycle.

The trace subsystem must remain observational. It must not allocate, block,
print from hot paths, or become part of syscall or scheduler correctness.

## Decision

Extend trace records with:

- `cycle`: the RISC-V cycle counter at emit time
- `arg1`: a second event-specific metadata word

Keep `tick` and `seq`. `tick` provides coarse scheduler-time context, `cycle`
prepares PR8 latency work, and `seq` remains the total ordering source.

Enable S-mode cycle-counter reads by setting `mcounteren.CY` in the M-mode
shim. This is a mechanism change only; userspace still does not receive direct
counter access.

Add hybrid lifecycle events:

- user syscall enter, return, and unsupported-syscall error
- user accelerator validation and copyback
- accelerator driver submit, complete, timeout, and reset

Emit events at ownership boundaries:

- `syscall.c` traces syscall dispatch lifecycle
- `accel_syscall.c` traces user pointer validation and copyback outcomes
- `accel.c` traces kernel descriptor and simulated device request lifecycle

Add a dedicated `runtime-tracing` C userspace program. It yields, sleeps,
performs one timed-out accelerator memset, verifies the user buffer was not
modified, performs one successful accelerator memset, verifies copyback, and
exits with code 0. The scenario observer delays hardware stepping long enough
to make the timeout deterministic, then steps the simulator and dispatches IRQs
until the user task exits.

The smoke test keeps functional output ordering exact, but validates trace
evidence by requiring key trace event types rather than an exact trace
interleaving.

## Consequences

Each trace event now costs another 16 bytes for `cycle` and `arg1`. With the
current 128-entry ring, that is still a small fixed memory cost. Overflow
policy remains unchanged: tracing overwrites old events and increments the
overwrite counter rather than changing kernel behavior.

Runtime trace output is more useful for reading boundary behavior:

- syscall number plus user PC on entry
- syscall number plus result on return
- accelerator length plus timeout on submit
- accelerator result plus descriptor status on completion

Driver completion events are attributed to the submitting thread, even when an
observer thread dispatches the simulated IRQ. This keeps trace ownership tied
to the request rather than the incidental dispatcher context.

## Alternatives Considered

Keeping only `arg0` would minimize churn, but syscall and accelerator events
would need to overload one field too heavily.

Adding a typed payload union would make each event self-describing in C, but it
would add machinery before the kernel has a real trace consumer.

Strict trace-order smoke validation would provide more exact evidence, but it
would also freeze incidental scheduler interleavings. Required event-type
coverage is the better invariant for this stage.

Deferring cycle timestamps to PR8 would keep PR7 purely semantic, but adding
the field now lets PR8 focus on measurement scenarios rather than changing the
core trace record again.

## Evidence

Expected smoke output includes:

```text
scenario: runtime-tracing
user: entering u-mode pc=... sp=... satp=...
user: syscall yield
user: syscall sleep ticks=...
user: accel memset timeout
user: accel memset
user: exited code=...
milestone 22: user address-space switching
user: runtime tracing passed
trace: begin count=... overwrites=...
trace: seq=... tick=... cycle=... type=user_syscall_enter tid=... other=... arg0=... arg1=...
trace: ...
trace: end
milestone 28: runtime tracing
```

Run:

```sh
make test SCENARIO=runtime-tracing
```

## Connections

The ECE350 connection is observability across protection boundaries: a syscall
is no longer just a function call, so evidence should show the trap boundary,
the scheduler-visible blocking point, and the device completion path.

The STM32 RTOS analogy is tracing around task switches and peripheral request
lifecycles. The analogy breaks at the userspace boundary: this kernel must also
show that user virtual addresses are validated and copied through a kernel-owned
boundary before a simulated device sees physical buffers.
