# DDR 35: Text-Only Userspace Runtime

## Status

Accepted

## Context

PR3 introduced a dispatcher-owned syscall ABI, but user programs still encoded
raw syscall numbers directly in assembly. That proved the kernel dispatch path,
but it did not establish a reusable userspace calling surface.

The next Stage 5 step needs user programs to call named runtime stubs. The repo
does not yet have an ELF loader, user data mapping, user BSS zeroing, dynamic
relocation, argc/env setup, or a libc.

The current `user_task_init()` contract is intentionally narrow:

```text
copy one program blob into one user-executable page at USER_SPACE_CODE_BASE
map one user stack page
enter at USER_SPACE_CODE_BASE
```

## Decision

Add a minimal freestanding C userspace build:

- shared ABI constants in `include/user_abi.h`
- user runtime declarations in `user/include/user/runtime.h`
- syscall stubs in `user/runtime.c`
- assembly `_start` in `user/start.S`
- a C smoke program in `user/programs/runtime_main.c`
- a user linker script in `user/linker.ld`

The user build links `_start` at `USER_SPACE_CODE_BASE`, converts the result to
a flat binary, and embeds that binary into the kernel image. The `user-runtime`
scenario passes the embedded bytes to `user_task_init()` just like the previous
assembly-only user programs.

The runtime stays text-only. The user linker script asserts:

```text
.text fits in one page
.rodata is empty
.data is empty
.bss is empty
```

## Consequences

User programs can now express the syscall boundary as normal calls:

```c
user_yield();
user_sleep(2);
return 0;
```

`_start` owns the first userspace entry convention. It calls `user_main()` and
passes the returned integer to `user_exit()`.

The syscall stubs own only the user-side ABI mechanics: place syscall numbers
and arguments in the agreed registers, execute `ecall`, and return `a0` for
syscalls that resume. They do not own kernel syscall policy, task lifetime, or
scheduling decisions.

This deliberately does not make C userspace general-purpose. A string literal,
global variable, static local, or compiler-emitted data section fails the user
link instead of silently producing bytes that `user_task_init()` does not map.

## Alternatives Considered

Keeping runtime stubs in assembly would have been smaller, but it would not
force a real user C entry convention or a user-only include surface.

Building a fuller user image with data and BSS support would be closer to a
normal process model, but it would require deciding loader semantics before
pointer-bearing syscalls and usercopy policy are ready.

Embedding an ELF and teaching the kernel to load it was rejected for this PR.
That would combine runtime, image format, segment mapping, zero-fill policy, and
validation into one step.

## Evidence

The generated `user-runtime` image is a flat binary produced from a user ELF
whose only loadable section is `.text`. The smoke program calls `user_yield()`
and `user_sleep(2)`, verifies both return `USER_SYSCALL_OK`, returns from
`user_main()`, and exits with that return code through `_start`.

Expected smoke output includes:

```text
scenario: user-runtime
user: syscall yield
user: syscall sleep ticks=...
user: exited code=...
milestone 22: user address-space switching
user: runtime stubs passed
milestone 25: userspace runtime
```

Run:

```sh
make test SCENARIO=user-runtime
```

## Connections

The ECE350 connection is the distinction between ABI and implementation. The
kernel exposes a register contract, while userspace wraps that contract in
ordinary callable functions.

The STM32 RTOS connection is the startup path: `_start` is like the first code a
task runs before its application function. The analogy stops at privilege:
RISC-V userspace also crosses a hardware-enforced U/S boundary on every syscall.
