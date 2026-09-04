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

typedef enum user_task_state {
    USER_TASK_UNUSED = 0,
    USER_TASK_READY,
    USER_TASK_EXITED,
    USER_TASK_DESTROYED
} user_task_state_t;

typedef struct user_task {
    user_task_state_t state;
    vm_space_t address_space;
    uint64_t user_satp;
    uint64_t exit_code;
    void *code_page;
    void *stack_page;
    user_trap_context_t *trap_context;
} user_task_t;

int user_task_init(user_task_t *task, const void *program, size_t program_size);
int user_task_set_kernel_sp(user_task_t *task, uintptr_t kernel_sp);
int user_task_mark_exited(user_task_t *task, uint64_t exit_code);
int user_task_destroy(user_task_t *task);
int user_task_is_ready(const user_task_t *task);
user_task_state_t user_task_state(const user_task_t *task);
uint64_t user_task_exit_code(const user_task_t *task);
trap_frame_t *user_task_trap_frame(user_task_t *task);
uint64_t user_task_satp(const user_task_t *task);

#endif
