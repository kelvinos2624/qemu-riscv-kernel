#ifndef KERNEL_USER_ACCEL_SYSCALL_H
#define KERNEL_USER_ACCEL_SYSCALL_H

#include "core/trap.h"

trap_frame_t *user_accel_syscall_memset(trap_frame_t *frame);

#endif
