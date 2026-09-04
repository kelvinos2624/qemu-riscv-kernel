#ifndef KERNEL_USER_SYSCALL_H
#define KERNEL_USER_SYSCALL_H

#include "core/trap.h"
#include "user_abi.h"

trap_frame_t *user_syscall_dispatch(trap_frame_t *frame);

#endif
