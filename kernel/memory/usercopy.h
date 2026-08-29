#ifndef KERNEL_MEMORY_USERCOPY_H
#define KERNEL_MEMORY_USERCOPY_H

#include <stddef.h>

#define USERCOPY_OK 0
#define USERCOPY_ERR_INVALID (-1)
#define USERCOPY_ERR_FAULT (-2)

int copy_from_user(void *dst, const void *user_src, size_t len);
int copy_to_user(void *user_dst, const void *src, size_t len);

/*
 * Scenario-only hook that bypasses validation to prove the recoverable
 * page-fault path. Production kernel code should use copy_from_user() and
 * copy_to_user().
 */
int usercopy_recoverable_fault_selftest(void);

#endif
