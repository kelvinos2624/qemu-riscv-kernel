#include "arch/riscv64/irq.h"
#include "core/kernel.h"
#include "memory/heap.h"
#include "memory/page_alloc.h"

#define HEAP_ALIGNMENT 16u
#define HEAP_MIN_CLASS 32u
#define HEAP_MAX_CLASS 2048u
#define HEAP_CLASS_COUNT 7u
#define HEAP_PAGE_MAGIC 0x5148525448454150ull
#define HEAP_BITMAP_WORDS ((PAGE_SIZE / HEAP_MIN_CLASS + 63u) / 64u)

typedef struct heap_free_block {
    struct heap_free_block *next;
} heap_free_block_t;

typedef struct heap_page {
    uint64_t magic;
    uint16_t block_size;
    uint16_t block_count;
    uint16_t free_count;
    uint16_t class_index;
    heap_free_block_t *free_list;
    struct heap_page *next;
    uint64_t allocated_bitmap[HEAP_BITMAP_WORDS];
} heap_page_t;

typedef struct heap_pool {
    size_t block_size;
    heap_page_t *pages;
} heap_pool_t;

static heap_pool_t heap_pools[HEAP_CLASS_COUNT];
static size_t heap_pages;
static size_t heap_free_blocks_bytes;
static size_t heap_allocated_blocks_bytes;
static int heap_initialized;

static const size_t heap_class_sizes[HEAP_CLASS_COUNT] = {
    32u,
    64u,
    128u,
    256u,
    512u,
    1024u,
    2048u,
};

static uintptr_t align_up(uintptr_t value, uintptr_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uintptr_t align_down(uintptr_t value, uintptr_t alignment)
{
    return value & ~(alignment - 1u);
}

static void memory_zero(void *ptr, size_t size)
{
    uint8_t *bytes = ptr;
    for (size_t i = 0; i < size; i++) {
        bytes[i] = 0;
    }
}

static int bitmap_test(const heap_page_t *page, size_t index)
{
    const size_t word = index / 64u;
    const size_t bit = index % 64u;
    return (page->allocated_bitmap[word] & (1ull << bit)) != 0;
}

static void bitmap_set(heap_page_t *page, size_t index)
{
    const size_t word = index / 64u;
    const size_t bit = index % 64u;
    page->allocated_bitmap[word] |= 1ull << bit;
}

static void bitmap_clear(heap_page_t *page, size_t index)
{
    const size_t word = index / 64u;
    const size_t bit = index % 64u;
    page->allocated_bitmap[word] &= ~(1ull << bit);
}

static uintptr_t block_area_start(const heap_page_t *page)
{
    return align_up((uintptr_t)(page + 1), HEAP_ALIGNMENT);
}

static int class_index_for_size(size_t size)
{
    if (size == 0 || size > HEAP_MAX_CLASS) {
        return -1;
    }

    for (size_t i = 0; i < HEAP_CLASS_COUNT; i++) {
        if (size <= heap_class_sizes[i]) {
            return (int)i;
        }
    }

    return -1;
}

static heap_page_t *heap_grow_pool(size_t class_index)
{
    void *raw_page = page_alloc();
    if (raw_page == NULL) {
        return NULL;
    }

    heap_page_t *page = raw_page;
    const size_t block_size = heap_pools[class_index].block_size;
    const uintptr_t block_start = block_area_start(page);
    const uintptr_t page_end = (uintptr_t)page + PAGE_SIZE;
    const size_t block_count = (page_end - block_start) / block_size;
    if (block_count == 0 || block_count > PAGE_SIZE / HEAP_MIN_CLASS) {
        PANIC("invalid heap block count");
    }

    page->magic = HEAP_PAGE_MAGIC;
    page->block_size = (uint16_t)block_size;
    page->block_count = (uint16_t)block_count;
    page->free_count = (uint16_t)block_count;
    page->class_index = (uint16_t)class_index;
    page->free_list = NULL;
    page->next = heap_pools[class_index].pages;
    for (size_t i = 0; i < HEAP_BITMAP_WORDS; i++) {
        page->allocated_bitmap[i] = 0;
    }

    for (size_t i = 0; i < block_count; i++) {
        heap_free_block_t *block =
            (heap_free_block_t *)(block_start + (i * block_size));
        block->next = page->free_list;
        page->free_list = block;
    }

    heap_pools[class_index].pages = page;
    heap_pages++;
    heap_free_blocks_bytes += block_count * block_size;
    return page;
}

static void *heap_alloc_block(heap_page_t *page)
{
    if (page->free_list == NULL || page->free_count == 0) {
        PANIC("heap page has no free blocks");
    }

    heap_free_block_t *block = page->free_list;
    page->free_list = block->next;

    const uintptr_t block_start = block_area_start(page);
    const uintptr_t block_addr = (uintptr_t)block;
    const size_t block_index = (block_addr - block_start) / page->block_size;
    if (block_index >= page->block_count || bitmap_test(page, block_index)) {
        PANIC("heap free list corruption");
    }

    bitmap_set(page, block_index);
    page->free_count--;
    heap_free_blocks_bytes -= page->block_size;
    heap_allocated_blocks_bytes += page->block_size;

    return block;
}

void heap_init(void)
{
    irq_state_t irq_state = irq_save();

    for (size_t i = 0; i < HEAP_CLASS_COUNT; i++) {
        heap_pools[i].block_size = heap_class_sizes[i];
        heap_pools[i].pages = NULL;
    }
    heap_pages = 0;
    heap_free_blocks_bytes = 0;
    heap_allocated_blocks_bytes = 0;
    heap_initialized = 1;

    irq_restore(irq_state);
}

void *kmalloc(size_t size)
{
    irq_state_t irq_state = irq_save();

    if (!heap_initialized) {
        PANIC("kmalloc before heap_init");
    }

    const int class_index = class_index_for_size(size);
    if (class_index < 0) {
        irq_restore(irq_state);
        return NULL;
    }

    heap_page_t *page = heap_pools[class_index].pages;
    while (page != NULL && page->free_count == 0) {
        page = page->next;
    }

    if (page == NULL) {
        page = heap_grow_pool((size_t)class_index);
        if (page == NULL) {
            irq_restore(irq_state);
            return NULL;
        }
    }

    void *block = heap_alloc_block(page);
    irq_restore(irq_state);
    return block;
}

void *kzalloc(size_t size)
{
    void *block = kmalloc(size);
    if (block == NULL) {
        return NULL;
    }

    heap_page_t *page = (heap_page_t *)align_down((uintptr_t)block, PAGE_SIZE);
    if (page->magic != HEAP_PAGE_MAGIC) {
        PANIC("kzalloc invalid heap page");
    }

    memory_zero(block, page->block_size);
    return block;
}

void kfree(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    irq_state_t irq_state = irq_save();

    if (!heap_initialized) {
        PANIC("kfree before heap_init");
    }

    const uintptr_t page_addr = align_down((uintptr_t)ptr, PAGE_SIZE);
    if (page_addr < page_managed_start() || page_addr >= page_managed_end()) {
        PANIC("kfree invalid heap address");
    }

    heap_page_t *page = (heap_page_t *)page_addr;
    if (page->magic != HEAP_PAGE_MAGIC) {
        PANIC("kfree invalid heap page");
    }

    const uintptr_t block_start = block_area_start(page);
    const uintptr_t block_addr = (uintptr_t)ptr;
    const uintptr_t page_end = (uintptr_t)page + PAGE_SIZE;
    if (block_addr < block_start || block_addr >= page_end) {
        PANIC("kfree pointer outside heap blocks");
    }

    const uintptr_t offset = block_addr - block_start;
    if ((offset % page->block_size) != 0) {
        PANIC("kfree interior pointer");
    }

    const size_t block_index = offset / page->block_size;
    if (block_index >= page->block_count) {
        PANIC("kfree block index out of range");
    }
    if (!bitmap_test(page, block_index)) {
        PANIC("double kfree");
    }

    bitmap_clear(page, block_index);
    heap_free_block_t *block = ptr;
    block->next = page->free_list;
    page->free_list = block;
    page->free_count++;
    heap_free_blocks_bytes += page->block_size;
    heap_allocated_blocks_bytes -= page->block_size;

    irq_restore(irq_state);
}

size_t heap_page_count(void)
{
    irq_state_t irq_state = irq_save();

    if (!heap_initialized) {
        PANIC("heap_page_count before heap_init");
    }

    const size_t count = heap_pages;
    irq_restore(irq_state);
    return count;
}

size_t heap_free_bytes(void)
{
    irq_state_t irq_state = irq_save();

    if (!heap_initialized) {
        PANIC("heap_free_bytes before heap_init");
    }

    const size_t bytes = heap_free_blocks_bytes;
    irq_restore(irq_state);
    return bytes;
}

size_t heap_allocated_bytes(void)
{
    irq_state_t irq_state = irq_save();

    if (!heap_initialized) {
        PANIC("heap_allocated_bytes before heap_init");
    }

    const size_t bytes = heap_allocated_blocks_bytes;
    irq_restore(irq_state);
    return bytes;
}
