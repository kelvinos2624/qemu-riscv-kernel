#ifndef KERNEL_MEMORY_VM_H
#define KERNEL_MEMORY_VM_H

#include <stdint.h>

#define VM_OK 0
#define VM_ERR_INVALID (-1)
#define VM_ERR_NO_MEMORY (-2)
#define VM_ERR_EXISTS (-3)
#define VM_ERR_NOT_MAPPED (-4)

#define VM_PTE_V (1ull << 0)
#define VM_PTE_R (1ull << 1)
#define VM_PTE_W (1ull << 2)
#define VM_PTE_X (1ull << 3)
#define VM_PTE_U (1ull << 4)
#define VM_PTE_G (1ull << 5)
#define VM_PTE_A (1ull << 6)
#define VM_PTE_D (1ull << 7)

#define VM_PAGE_OFFSET_MASK 0xfffull
#define VM_TRANSLATE_INVALID UINTPTR_MAX

typedef uint64_t pte_t;

typedef struct vm_space {
    pte_t *root;
} vm_space_t;

int vm_space_init(vm_space_t *space);
int vm_map_page(vm_space_t *space, uintptr_t va, uintptr_t pa, uint64_t flags);
int vm_unmap_page(vm_space_t *space, uintptr_t va);
uintptr_t vm_translate(const vm_space_t *space, uintptr_t va);

#endif
