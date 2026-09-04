#include "memory/page_alloc.h"
#include "memory/user_space.h"
#include "memory/vm.h"

#define USER_SPACE_CLINT_BASE ((uintptr_t)0x02000000)
#define USER_SPACE_CLINT_END ((uintptr_t)0x02010000)
#define USER_SPACE_UART0_BASE ((uintptr_t)0x10000000)
#define USER_SPACE_UART0_END (USER_SPACE_UART0_BASE + PAGE_SIZE)

extern char __trampoline_start[];
extern char __trampoline_end[];

static int ranges_overlap(
    uintptr_t a_start,
    uintptr_t a_end,
    uintptr_t b_start,
    uintptr_t b_end
)
{
    return a_start < b_end && b_start < a_end;
}

static int range_overlaps_platform_mmio(uintptr_t va, uintptr_t end)
{
    return ranges_overlap(va, end, USER_SPACE_CLINT_BASE, USER_SPACE_CLINT_END) ||
           ranges_overlap(va, end, USER_SPACE_UART0_BASE, USER_SPACE_UART0_END);
}

static int user_pa_range_is_valid(uintptr_t pa, uintptr_t size)
{
    const uintptr_t end = pa + size;

    if (size == 0 || end < pa) {
        return 0;
    }

    return pa >= page_managed_start() && end <= page_managed_end();
}

int user_space_va_range_is_valid(uintptr_t va, size_t size)
{
    const uintptr_t end = va + size;

    if (size == 0 || end < va) {
        return 0;
    }

    if (va < USER_SPACE_BASE || end - 1u > USER_SPACE_TOP) {
        return 0;
    }

    return !range_overlaps_platform_mmio(va, end);
}

int user_space_map_page(
    vm_space_t *space,
    uintptr_t va,
    uintptr_t pa,
    uint64_t flags
)
{
    if (!user_space_va_range_is_valid(va, PAGE_SIZE) ||
        !user_pa_range_is_valid(pa, PAGE_SIZE) ||
        (flags & VM_PTE_U) == 0 ||
        (flags & VM_PTE_G) != 0) {
        return VM_ERR_INVALID;
    }

    return vm_map_page(space, va, pa, flags);
}

int user_space_map_code_page(vm_space_t *space, uintptr_t pa)
{
    return user_space_map_page(
        space,
        USER_SPACE_CODE_BASE,
        pa,
        VM_PTE_V | VM_PTE_R | VM_PTE_X | VM_PTE_U | VM_PTE_A
    );
}

int user_space_map_stack_page(vm_space_t *space, uintptr_t pa)
{
    return user_space_map_page(
        space,
        USER_SPACE_STACK_BASE,
        pa,
        VM_PTE_V | VM_PTE_R | VM_PTE_W | VM_PTE_U | VM_PTE_A | VM_PTE_D
    );
}

int user_space_map_trap_support(vm_space_t *space, uintptr_t trap_context_pa)
{
    const uintptr_t trampoline_start = (uintptr_t)__trampoline_start;
    const uintptr_t trampoline_end = (uintptr_t)__trampoline_end;

    if (space == NULL ||
        space->root == NULL ||
        trampoline_start == trampoline_end ||
        trampoline_end - trampoline_start > PAGE_SIZE ||
        (trampoline_start & PAGE_MASK) != 0 ||
        !user_pa_range_is_valid(trap_context_pa, PAGE_SIZE)) {
        return VM_ERR_INVALID;
    }

    const uint64_t trampoline_flags = VM_PTE_V | VM_PTE_R | VM_PTE_X | VM_PTE_A;
    const uint64_t context_flags =
        VM_PTE_V | VM_PTE_R | VM_PTE_W | VM_PTE_A | VM_PTE_D;

    int result = vm_map_page(
        space,
        USER_TRAMPOLINE_VA,
        trampoline_start,
        trampoline_flags
    );
    if (result != VM_OK) {
        return result;
    }

    result = vm_map_page(
        space,
        USER_TRAP_CONTEXT_VA,
        trap_context_pa,
        context_flags
    );
    if (result != VM_OK) {
        (void)vm_unmap_page(space, USER_TRAMPOLINE_VA);
        return result;
    }

    return VM_OK;
}
