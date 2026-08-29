#include "arch/riscv64/irq.h"
#include "core/kernel.h"
#include "memory/page_alloc.h"
#include "memory/vm.h"

#define SV39_LEVELS 3u
#define SV39_ENTRIES_PER_TABLE 512u
#define SV39_VPN_BITS 9u
#define SV39_PPN_SHIFT 10u
#define SV39_TOP_BIT 38u

static void memory_zero(void *ptr, size_t size)
{
    uint8_t *bytes = ptr;
    for (size_t i = 0; i < size; i++) {
        bytes[i] = 0;
    }
}

static int page_aligned(uintptr_t addr)
{
    return (addr & PAGE_MASK) == 0;
}

static int va_is_canonical(uintptr_t va)
{
    const uint64_t sign = (uint64_t)1u << SV39_TOP_BIT;
    const uint64_t high_mask = ~((sign << 1u) - 1u);

    if ((va & sign) == 0) {
        return (va & high_mask) == 0;
    }

    return (va & high_mask) == high_mask;
}

static int pte_is_valid(pte_t pte)
{
    return (pte & VM_PTE_V) != 0;
}

static int pte_is_leaf(pte_t pte)
{
    return (pte & (VM_PTE_R | VM_PTE_W | VM_PTE_X)) != 0;
}

static uintptr_t pte_to_pa(pte_t pte)
{
    return (uintptr_t)((pte >> SV39_PPN_SHIFT) << 12);
}

static pte_t pa_to_pte(uintptr_t pa, uint64_t flags)
{
    return ((pte_t)(pa >> 12) << SV39_PPN_SHIFT) | flags;
}

static size_t vpn_index(uintptr_t va, unsigned int level)
{
    const unsigned int shift = 12u + (level * SV39_VPN_BITS);
    return (size_t)((va >> shift) & (SV39_ENTRIES_PER_TABLE - 1u));
}

static int flags_are_valid(uint64_t flags)
{
    const uint64_t known_flags = VM_PTE_V |
        VM_PTE_R |
        VM_PTE_W |
        VM_PTE_X |
        VM_PTE_U |
        VM_PTE_G |
        VM_PTE_A |
        VM_PTE_D;

    if ((flags & ~known_flags) != 0) {
        return 0;
    }

    if ((flags & VM_PTE_V) == 0) {
        return 0;
    }

    if ((flags & (VM_PTE_R | VM_PTE_X)) == 0) {
        return 0;
    }

    if ((flags & VM_PTE_W) != 0 && (flags & VM_PTE_R) == 0) {
        return 0;
    }

    return 1;
}

static void rollback_allocated_tables(
    pte_t **allocated_ptes,
    pte_t **allocated_tables,
    size_t allocated_count
)
{
    while (allocated_count > 0) {
        allocated_count--;
        *allocated_ptes[allocated_count] = 0;
        page_free(allocated_tables[allocated_count]);
    }
}

static int table_contains_leaf(const pte_t *table, unsigned int level)
{
    for (size_t i = 0; i < SV39_ENTRIES_PER_TABLE; i++) {
        const pte_t pte = table[i];
        if (!pte_is_valid(pte)) {
            continue;
        }

        if (pte_is_leaf(pte) || level == 0) {
            return 1;
        }

        if (table_contains_leaf((const pte_t *)pte_to_pa(pte), level - 1u)) {
            return 1;
        }
    }

    return 0;
}

static void free_empty_table_children(pte_t *table, unsigned int level)
{
    if (level == 0) {
        return;
    }

    for (size_t i = 0; i < SV39_ENTRIES_PER_TABLE; i++) {
        const pte_t pte = table[i];
        if (!pte_is_valid(pte)) {
            continue;
        }

        pte_t *child = (pte_t *)pte_to_pa(pte);
        free_empty_table_children(child, level - 1u);
        page_free(child);
        table[i] = 0;
    }
}

int vm_space_init(vm_space_t *space)
{
    if (space == NULL) {
        return VM_ERR_INVALID;
    }

    irq_state_t irq_state = irq_save();

    pte_t *root = page_alloc();
    if (root == NULL) {
        irq_restore(irq_state);
        return VM_ERR_NO_MEMORY;
    }

    memory_zero(root, PAGE_SIZE);
    space->root = root;

    irq_restore(irq_state);
    return VM_OK;
}

int vm_space_destroy(vm_space_t *space)
{
    if (space == NULL || space->root == NULL) {
        return VM_ERR_INVALID;
    }

    irq_state_t irq_state = irq_save();

    if (table_contains_leaf(space->root, SV39_LEVELS - 1u)) {
        irq_restore(irq_state);
        return VM_ERR_BUSY;
    }

    free_empty_table_children(space->root, SV39_LEVELS - 1u);
    page_free(space->root);
    space->root = NULL;

    irq_restore(irq_state);
    return VM_OK;
}

static pte_t *vm_walk(const vm_space_t *space, uintptr_t va, int alloc)
{
    if (space == NULL || space->root == NULL || !va_is_canonical(va)) {
        return NULL;
    }

    pte_t *allocated_ptes[SV39_LEVELS - 1u];
    pte_t *allocated_tables[SV39_LEVELS - 1u];
    size_t allocated_count = 0;

    pte_t *table = space->root;
    for (int level = (int)SV39_LEVELS - 1; level > 0; level--) {
        pte_t *pte = &table[vpn_index(va, (unsigned int)level)];
        if (pte_is_valid(*pte)) {
            if (pte_is_leaf(*pte)) {
                return NULL;
            }

            table = (pte_t *)pte_to_pa(*pte);
            continue;
        }

        if (!alloc) {
            return NULL;
        }

        pte_t *next_table = page_alloc();
        if (next_table == NULL) {
            rollback_allocated_tables(
                allocated_ptes,
                allocated_tables,
                allocated_count
            );
            return NULL;
        }

        memory_zero(next_table, PAGE_SIZE);
        *pte = pa_to_pte((uintptr_t)next_table, VM_PTE_V);
        allocated_ptes[allocated_count] = pte;
        allocated_tables[allocated_count] = next_table;
        allocated_count++;
        table = next_table;
    }

    return &table[vpn_index(va, 0)];
}

int vm_map_page(vm_space_t *space, uintptr_t va, uintptr_t pa, uint64_t flags)
{
    if (space == NULL ||
        space->root == NULL ||
        !va_is_canonical(va) ||
        !page_aligned(va) ||
        !page_aligned(pa) ||
        !flags_are_valid(flags)) {
        return VM_ERR_INVALID;
    }

    irq_state_t irq_state = irq_save();

    pte_t *pte = vm_walk(space, va, 1);
    if (pte == NULL) {
        irq_restore(irq_state);
        return VM_ERR_NO_MEMORY;
    }

    if (pte_is_valid(*pte)) {
        irq_restore(irq_state);
        return VM_ERR_EXISTS;
    }

    *pte = pa_to_pte(pa, flags);

    irq_restore(irq_state);
    return VM_OK;
}

int vm_unmap_page(vm_space_t *space, uintptr_t va)
{
    if (space == NULL ||
        space->root == NULL ||
        !va_is_canonical(va) ||
        !page_aligned(va)) {
        return VM_ERR_INVALID;
    }

    irq_state_t irq_state = irq_save();

    pte_t *pte = vm_walk(space, va, 0);
    if (pte == NULL || !pte_is_valid(*pte) || !pte_is_leaf(*pte)) {
        irq_restore(irq_state);
        return VM_ERR_NOT_MAPPED;
    }

    /* Empty intermediate page-table reclamation is deferred; see DDR 20. */
    *pte = 0;

    irq_restore(irq_state);
    return VM_OK;
}

uintptr_t vm_translate(const vm_space_t *space, uintptr_t va)
{
    irq_state_t irq_state = irq_save();

    pte_t *pte = vm_walk(space, va, 0);
    if (pte == NULL || !pte_is_valid(*pte) || !pte_is_leaf(*pte)) {
        irq_restore(irq_state);
        return VM_TRANSLATE_INVALID;
    }

    const uintptr_t pa = pte_to_pa(*pte) | (va & VM_PAGE_OFFSET_MASK);

    irq_restore(irq_state);
    return pa;
}
