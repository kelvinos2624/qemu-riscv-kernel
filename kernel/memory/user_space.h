#ifndef KERNEL_MEMORY_USER_SPACE_H
#define KERNEL_MEMORY_USER_SPACE_H

#include "memory/page_alloc.h"
#include "memory/vm.h"

#include <stddef.h>
#include <stdint.h>

#define USER_SPACE_BASE PAGE_SIZE
#define USER_SPACE_CODE_BASE USER_SPACE_BASE
#define USER_SPACE_STACK_TOP ((uintptr_t)0x0000000040000000ull)
#define USER_SPACE_STACK_BASE (USER_SPACE_STACK_TOP - PAGE_SIZE)
#define USER_SPACE_TOP USER_SPACE_STACK_TOP

int user_space_va_range_is_valid(uintptr_t va, size_t size);
int user_space_map_page(
    vm_space_t *space,
    uintptr_t va,
    uintptr_t pa,
    uint64_t flags
);
int user_space_map_code_page(vm_space_t *space, uintptr_t pa);
int user_space_map_stack_page(vm_space_t *space, uintptr_t pa);

#endif
