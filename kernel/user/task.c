#include "arch/riscv64/csr.h"
#include "core/kernel.h"
#include "core/trap.h"
#include "memory/page_alloc.h"
#include "memory/paging.h"
#include "memory/user_space.h"
#include "memory/vm.h"
#include "user/task.h"

#include <stddef.h>

extern void trap_entry(void);

_Static_assert(offsetof(user_trap_context_t, frame) == 0, "trap context frame offset");
_Static_assert(
    offsetof(user_trap_context_t, kernel_satp) == 280,
    "trap context kernel_satp offset"
);
_Static_assert(
    offsetof(user_trap_context_t, user_satp) == 288,
    "trap context user_satp offset"
);
_Static_assert(
    offsetof(user_trap_context_t, kernel_trap_handler) == 296,
    "trap context kernel_trap_handler offset"
);
_Static_assert(
    offsetof(user_trap_context_t, kernel_trap_context) == 304,
    "trap context kernel_trap_context offset"
);
_Static_assert(
    offsetof(user_trap_context_t, kernel_stvec) == 312,
    "trap context kernel_stvec offset"
);
_Static_assert(
    offsetof(user_trap_context_t, kernel_sp) == 320,
    "trap context kernel_sp offset"
);
_Static_assert(sizeof(user_trap_context_t) <= PAGE_SIZE, "trap context fits in a page");

static void memory_copy(void *dst, const void *src, size_t size)
{
    uint8_t *dst_bytes = dst;
    const uint8_t *src_bytes = src;
    for (size_t i = 0; i < size; i++) {
        dst_bytes[i] = src_bytes[i];
    }
}

static void memory_zero(void *ptr, size_t size)
{
    uint8_t *bytes = ptr;
    for (size_t i = 0; i < size; i++) {
        bytes[i] = 0;
    }
}

static void user_task_reset(user_task_t *task)
{
    task->address_space.root = NULL;
    task->user_satp = 0;
    task->code_page = NULL;
    task->stack_page = NULL;
    task->trap_context = NULL;
}

static void user_task_cleanup_partial(user_task_t *task)
{
    if (task->address_space.root != NULL) {
        (void)vm_unmap_page(&task->address_space, USER_SPACE_CODE_BASE);
        (void)vm_unmap_page(&task->address_space, USER_SPACE_STACK_BASE);
        (void)vm_unmap_page(&task->address_space, USER_TRAMPOLINE_VA);
        (void)vm_unmap_page(&task->address_space, USER_TRAP_CONTEXT_VA);
        (void)vm_space_destroy(&task->address_space);
    }

    if (task->code_page != NULL) {
        page_free(task->code_page);
    }

    if (task->stack_page != NULL) {
        page_free(task->stack_page);
    }

    if (task->trap_context != NULL) {
        page_free(task->trap_context);
    }

    user_task_reset(task);
}

int user_task_init(user_task_t *task, const void *program, size_t program_size)
{
    if (task == NULL ||
        program == NULL ||
        program_size == 0 ||
        program_size > PAGE_SIZE) {
        return -1;
    }

    user_task_reset(task);

    if (vm_space_init(&task->address_space) != VM_OK) {
        user_task_reset(task);
        return -1;
    }

    task->code_page = page_alloc();
    task->stack_page = page_alloc();
    task->trap_context = page_alloc();
    if (task->code_page == NULL ||
        task->stack_page == NULL ||
        task->trap_context == NULL) {
        user_task_cleanup_partial(task);
        return -1;
    }

    memory_zero(task->code_page, PAGE_SIZE);
    memory_zero(task->stack_page, PAGE_SIZE);
    memory_zero(task->trap_context, PAGE_SIZE);
    memory_copy(task->code_page, program, program_size);
    __asm__ volatile("fence.i" : : : "memory");

    if (user_space_map_code_page(
            &task->address_space,
            (uintptr_t)task->code_page
        ) != VM_OK ||
        user_space_map_stack_page(
            &task->address_space,
            (uintptr_t)task->stack_page
        ) != VM_OK ||
        user_space_map_trap_support(
            &task->address_space,
            (uintptr_t)task->trap_context
        ) != VM_OK) {
        user_task_cleanup_partial(task);
        return -1;
    }

    task->user_satp =
        SATP_MODE_SV39 | ((uint64_t)(uintptr_t)task->address_space.root >> 12);

    user_trap_context_t *context = task->trap_context;
    context->kernel_satp = paging_kernel_satp();
    context->user_satp = task->user_satp;
    context->kernel_trap_handler = (uint64_t)(uintptr_t)trap_handle_user;
    context->kernel_trap_context = (uint64_t)(uintptr_t)context;
    context->kernel_stvec = (uint64_t)(uintptr_t)trap_entry;
    context->kernel_sp = 0;
    context->frame.sp = USER_SPACE_STACK_TOP;
    context->frame.mepc = USER_SPACE_CODE_BASE;
    context->frame.mstatus = SSTATUS_SPIE;

    return 0;
}

trap_frame_t *user_task_trap_frame(user_task_t *task)
{
    if (task == NULL || task->trap_context == NULL) {
        return NULL;
    }

    return &task->trap_context->frame;
}

uint64_t user_task_satp(const user_task_t *task)
{
    return task == NULL ? 0 : task->user_satp;
}
