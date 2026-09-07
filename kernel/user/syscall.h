#ifndef KERNEL_USER_SYSCALL_H
#define KERNEL_USER_SYSCALL_H

#include "core/trap.h"
#include "user_abi.h"

#include <stdint.h>

typedef struct user_syscall_benchmark {
    uint64_t iterations;
    uint64_t cycles_total;
    uint64_t cycles_min;
    uint64_t cycles_max;
} user_syscall_benchmark_t;

trap_frame_t *user_syscall_dispatch(trap_frame_t *frame);
void user_syscall_benchmark_reset(void);
user_syscall_benchmark_t user_syscall_benchmark_snapshot(void);

#endif
