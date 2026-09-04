#ifndef KERNEL_USER_SYSCALL_H
#define KERNEL_USER_SYSCALL_H

#include "core/trap.h"

#include <stdint.h>

#define USER_SYSCALL_EXIT 1u
#define USER_SYSCALL_YIELD 2u
#define USER_SYSCALL_SLEEP 3u

#define USER_SYSCALL_OK 0

trap_frame_t *user_syscall_dispatch(trap_frame_t *frame);

#endif
