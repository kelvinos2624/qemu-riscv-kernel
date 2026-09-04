#ifndef KERNEL_USER_TASK_H
#define KERNEL_USER_TASK_H

#include "core/trap.h"
#include "memory/vm.h"

#include <stddef.h>
#include <stdint.h>

typedef struct user_trap_context {
    trap_frame_t frame;
    uint64_t kernel_satp;
    uint64_t user_satp;
    uint64_t kernel_trap_handler;
    uint64_t kernel_trap_context;
    uint64_t kernel_stvec;
    uint64_t kernel_sp;
} user_trap_context_t;

typedef struct user_task {
    vm_space_t address_space;
    uint64_t user_satp;
    void *code_page;
    void *stack_page;
    user_trap_context_t *trap_context;
} user_task_t;

int user_task_init(user_task_t *task, const void *program, size_t program_size);
trap_frame_t *user_task_trap_frame(user_task_t *task);
uint64_t user_task_satp(const user_task_t *task);

#endif
