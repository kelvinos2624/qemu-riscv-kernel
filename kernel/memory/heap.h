#ifndef KERNEL_MEMORY_HEAP_H
#define KERNEL_MEMORY_HEAP_H

#include <stddef.h>

void heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *ptr);
size_t heap_page_count(void);
size_t heap_free_bytes(void);
size_t heap_allocated_bytes(void);

#endif
