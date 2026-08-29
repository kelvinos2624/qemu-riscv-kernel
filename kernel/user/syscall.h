#ifndef KERNEL_USER_SYSCALL_H
#define KERNEL_USER_SYSCALL_H

#include "core/trap.h"

#include <stdint.h>

#define USER_SYSCALL_EXIT 1u

int user_syscall_handle(trap_frame_t *frame);

#endif
