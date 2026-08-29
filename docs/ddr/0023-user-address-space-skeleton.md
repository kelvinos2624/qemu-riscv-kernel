# DDR 23: Add User Mapping Policy Without Defining Processes Yet

## Status

Accepted

## Context

Stage 3 needs a user address-space skeleton before the kernel can enter U-mode.
The repo already has `vm_space_t` as the Sv39 page-table mechanism, but it does
not yet have process lifetime, U-mode trap return, syscall ABI, usercopy, or
frame ownership metadata.

This milestone should therefore prove that the kernel can build a bounded user
mapping container without prematurely deciding what a process is.

## Decision

Keep `vm_space_t` as the address-space mechanism and add a small
`user_space.c` policy layer over it.

The VM layer owns:

- Sv39 page-table allocation and walking
- PTE encoding and validation
- map, unmap, and translate mechanics
- page-table structure teardown

The user-space layer owns:

- the current sparse user VA layout
- null-page guard enforcement
- user mapping permission policy
- temporary QEMU `virt` MMIO virtual-address holes
- rejection of physical pages outside allocator-managed RAM

Add `vm_space_destroy()`, but make it reclaim page-table structure pages only.
It returns `VM_ERR_BUSY` if any live leaf mapping remains. Callers must unmap
leaf mappings and free any physical frames they own before destroying the
address space.

Add a nullable `address_space` pointer to `thread_t`, but do not make the
scheduler switch `satp` yet.

## Rationale

This keeps mechanism and policy separated. Generic VM answers whether Sv39 can
represent a mapping. User-space helpers answer whether this kernel currently
allows that mapping for a future user task.

The sparse layout leaves page zero unmapped, places code low, and places the
initial stack page at the top of the current user range:

```text
0x0000000000000000 - 0x0000000000000fff   null guard
0x0000000000001000                         code page
0x000000003ffff000                         stack page
0x0000000040000000                         user top / stack top
```

The mapped footprint is still only one code page and one stack page. The large
gap keeps the layout close to a real process address space without requiring a
loader, heap, or userspace runtime yet.

`vm_space_destroy()` refuses live leaf mappings because a mapping does not imply
ownership of the mapped physical frame. Failing while leaves remain makes frame
ownership bugs visible near the source instead of hiding leaks behind a
successful destroy.

## Alternatives Considered

### User-Space Wrapper Type

A separate `user_space_t` containing `vm_space_t` would make user ownership more
explicit, but it would also imply lifecycle semantics the kernel does not have
yet.

### Put User Policy Directly in VM

Adding user range checks to `vm_map_page()` would be simple, but it would make
generic page-table code aware of one policy domain. Kernel mappings, user
mappings, MMIO mappings, trampoline mappings, and future shared driver buffers
will not all share the same rules.

### Define Process Ownership Now

Attaching a full process model to this PR would force decisions about address
space sharing, task exit, user stack lifetime, and `satp` switching before the
kernel can execute U-mode code.

### Destroy With Leaf Callback

A callback-based destroy could let callers reclaim leaf frames according to
their own ownership policy. That is likely useful later, but it is more
abstraction than this kernel needs before usercopy, sharing, or process objects.

## Consequences

The user-space helper currently rejects QEMU `virt` CLINT and UART virtual
address holes. This is a temporary layout guard tied to the current identity
kernel mapping and should later move behind a platform memory-map abstraction.

The helper also rejects physical addresses outside allocator-managed RAM. That
prevents obvious kernel-image and MMIO mappings, but it is not complete frame
ownership tracking. Until ownership metadata exists, callers must pass frames
they allocated or otherwise own.

The new `thread_t.address_space` field is a placeholder only. DDR 24 records
the first U-mode transition choice: prove privilege crossing before wiring the
scheduler to separate user `satp` values.

## ECE350 and STM32 RTOS Connection

This maps to the ECE350 idea of an address space as a protection container:
valid user mappings are explicit, page zero remains invalid, and permissions
matter before execution begins.

The STM32 RTOS analogy is limited because the lab did not use Sv39 page tables.
The transferable lesson is ownership discipline: the structure that records a
mapping is not automatically the owner of the backing memory.

## Evidence

The QEMU smoke test observes:

```text
scenario: user-space
milestone 14: user address space skeleton
```
