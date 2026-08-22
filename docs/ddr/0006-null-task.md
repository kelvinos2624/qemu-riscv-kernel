# DDR 6: Reserve TID 0 for the Null Task

## Status

Accepted

## Context

The scheduler needs defined behavior when no real work is runnable. Without an
idle thread, every scheduler caller must handle a special no-thread case.

## Decision

Reserve thread ID 0 for a null task. The null task is created during
`thread_init()`, idles with `wfi`, yields after interrupts, never exits, and is
selected only when no non-null thread is ready.

## Alternatives Considered

- Return `NULL` from the scheduler when no thread is runnable.
- Keep idle behavior in `kmain`.
- Treat the null task as an ordinary round-robin participant.

## Consequences

The scheduler always returns a valid thread, which keeps context-switch paths
simple. The null task must be excluded from normal round-robin rotation so it
does not take CPU time while real threads are ready. Yielding after `wfi` gives
future interrupt-driven wakeups a scheduler handoff point without adding a
special idle escape path.

## Evidence

`kernel/core/thread.c` creates the null task at TID 0 and falls back to it from
`pick_next_thread()`.
