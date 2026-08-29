#ifndef KERNEL_MEMORY_PAGING_H
#define KERNEL_MEMORY_PAGING_H

#include "memory/vm.h"

#include <stdint.h>

uint64_t paging_init_kernel(void);
vm_space_t *paging_kernel_space(void);

#endif
