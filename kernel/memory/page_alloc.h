#ifndef KERNEL_MEMORY_PAGE_ALLOC_H
#define KERNEL_MEMORY_PAGE_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096u
#define PAGE_MASK ((uintptr_t)(PAGE_SIZE - 1u))

void page_init(uintptr_t mem_start, uintptr_t mem_end);
void *page_alloc(void);
void page_free(void *page);
size_t page_free_count(void);
size_t page_total_count(void);
uintptr_t page_managed_start(void);
uintptr_t page_managed_end(void);

#endif
