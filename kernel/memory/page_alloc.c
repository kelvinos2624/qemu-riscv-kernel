#include "arch/riscv64/irq.h"
#include "core/kernel.h"
#include "memory/page_alloc.h"

#define BITMAP_WORD_BITS 64u

extern char __page_bitmap_start[];
extern char __page_bitmap_end[];

static uint64_t *page_bitmap;
static size_t page_bitmap_words;
static uintptr_t managed_start;
static uintptr_t managed_end;
static size_t total_pages;
static size_t free_pages;
static size_t next_free_hint;
static int page_allocator_initialized;

static uintptr_t align_up(uintptr_t value, uintptr_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uintptr_t align_down(uintptr_t value, uintptr_t alignment)
{
    return value & ~(alignment - 1u);
}

static size_t bitmap_capacity_pages(void)
{
    return page_bitmap_words * BITMAP_WORD_BITS;
}

static int bitmap_test(size_t index)
{
    const size_t word = index / BITMAP_WORD_BITS;
    const size_t bit = index % BITMAP_WORD_BITS;
    return (page_bitmap[word] & (1ull << bit)) != 0;
}

static void bitmap_set(size_t index)
{
    const size_t word = index / BITMAP_WORD_BITS;
    const size_t bit = index % BITMAP_WORD_BITS;
    page_bitmap[word] |= 1ull << bit;
}

static void bitmap_clear(size_t index)
{
    const size_t word = index / BITMAP_WORD_BITS;
    const size_t bit = index % BITMAP_WORD_BITS;
    page_bitmap[word] &= ~(1ull << bit);
}

static uintptr_t page_addr_from_index(size_t index)
{
    return managed_start + (index * PAGE_SIZE);
}

static size_t page_index_from_addr(uintptr_t addr)
{
    return (addr - managed_start) / PAGE_SIZE;
}

void page_init(uintptr_t mem_start, uintptr_t mem_end)
{
    irq_state_t irq_state = irq_save();

    page_bitmap = (uint64_t *)(uintptr_t)__page_bitmap_start;
    const uintptr_t bitmap_start = (uintptr_t)__page_bitmap_start;
    const uintptr_t bitmap_end = (uintptr_t)__page_bitmap_end;
    const size_t bitmap_bytes = bitmap_end - bitmap_start;

    if ((bitmap_bytes % sizeof(uint64_t)) != 0) {
        PANIC("page bitmap is not word aligned");
    }

    page_bitmap_words = bitmap_bytes / sizeof(uint64_t);
    for (size_t i = 0; i < page_bitmap_words; i++) {
        page_bitmap[i] = 0;
    }

    managed_start = align_up(mem_start, PAGE_SIZE);
    managed_end = align_down(mem_end, PAGE_SIZE);
    if (managed_end < managed_start) {
        PANIC("invalid physical memory range");
    }

    total_pages = (managed_end - managed_start) / PAGE_SIZE;
    if (total_pages > bitmap_capacity_pages()) {
        PANIC("page bitmap too small");
    }

    free_pages = total_pages;
    next_free_hint = 0;
    page_allocator_initialized = 1;

    irq_restore(irq_state);
}

void *page_alloc(void)
{
    irq_state_t irq_state = irq_save();

    if (!page_allocator_initialized) {
        PANIC("page_alloc before page_init");
    }

    for (size_t offset = 0; offset < total_pages; offset++) {
        const size_t i = (next_free_hint + offset) % total_pages;
        if (bitmap_test(i)) {
            continue;
        }

        bitmap_set(i);
        free_pages--;
        next_free_hint = total_pages == 0 ? 0 : (i + 1u) % total_pages;
        void *page = (void *)page_addr_from_index(i);
        irq_restore(irq_state);
        return page;
    }

    irq_restore(irq_state);
    return NULL;
}

void page_free(void *page)
{
    irq_state_t irq_state = irq_save();

    if (!page_allocator_initialized) {
        PANIC("page_free before page_init");
    }

    const uintptr_t addr = (uintptr_t)page;
    if ((addr & PAGE_MASK) != 0 ||
        addr < managed_start ||
        addr >= managed_end) {
        PANIC("invalid page_free address");
    }

    const size_t index = page_index_from_addr(addr);
    if (!bitmap_test(index)) {
        PANIC("double page_free");
    }

    bitmap_clear(index);
    free_pages++;
    if (index < next_free_hint) {
        next_free_hint = index;
    }

    irq_restore(irq_state);
}

size_t page_free_count(void)
{
    irq_state_t irq_state = irq_save();

    if (!page_allocator_initialized) {
        PANIC("page_free_count before page_init");
    }

    const size_t count = free_pages;
    irq_restore(irq_state);
    return count;
}

size_t page_total_count(void)
{
    irq_state_t irq_state = irq_save();

    if (!page_allocator_initialized) {
        PANIC("page_total_count before page_init");
    }

    const size_t count = total_pages;
    irq_restore(irq_state);
    return count;
}

uintptr_t page_managed_start(void)
{
    return managed_start;
}

uintptr_t page_managed_end(void)
{
    return managed_end;
}
