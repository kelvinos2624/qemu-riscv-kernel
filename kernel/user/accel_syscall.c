#include "core/kernel.h"
#include "core/thread.h"
#include "core/trap.h"
#include "drivers/accel.h"
#include "drivers/accel_cmd.h"
#include "memory/page_alloc.h"
#include "memory/usercopy.h"
#include "user/accel_syscall.h"
#include "user/task.h"
#include "user_accel.h"

_Static_assert(USER_ACCEL_OK == ACCEL_OK, "user accel ok value");
_Static_assert(USER_ACCEL_ERR_NO_DEVICE == ACCEL_ERR_NO_DEVICE, "user accel no device value");
_Static_assert(USER_ACCEL_ERR_INVALID == ACCEL_ERR_INVALID, "user accel invalid value");
_Static_assert(USER_ACCEL_ERR_BUSY == ACCEL_ERR_BUSY, "user accel busy value");
_Static_assert(USER_ACCEL_ERR_IO == ACCEL_ERR_IO, "user accel io value");
_Static_assert(USER_ACCEL_ERR_TIMEOUT == ACCEL_ERR_TIMEOUT, "user accel timeout value");

static void memory_zero(void *ptr, size_t size)
{
    uint8_t *bytes = ptr;
    for (size_t i = 0; i < size; i++) {
        bytes[i] = 0;
    }
}

static int user_accel_copyback_result(int result)
{
    if (result == ACCEL_OK) {
        return USER_ACCEL_OK;
    }

    return result;
}

static int user_accel_memset_impl(
    user_task_t *task,
    uintptr_t user_dst,
    uint64_t len_arg,
    uint32_t value,
    uint64_t timeout_ticks
)
{
    if (len_arg == 0 || len_arg > USER_ACCEL_MEMSET_MAX_LEN) {
        return USER_ACCEL_ERR_INVALID;
    }

    const size_t len = (size_t)len_arg;
    int result = usercopy_task_validate(task, user_dst, len, 1);
    if (result != USERCOPY_OK) {
        return USER_ACCEL_ERR_INVALID;
    }

    accel_cmd_t *cmd = page_alloc();
    void *bounce_page = page_alloc();
    if (cmd == NULL || bounce_page == NULL) {
        if (cmd != NULL) {
            page_free(cmd);
        }
        if (bounce_page != NULL) {
            page_free(bounce_page);
        }
        return USER_ACCEL_ERR_NO_MEMORY;
    }

    memory_zero(cmd, sizeof(*cmd));
    cmd->op = ACCEL_CMD_OP_MEMSET;
    cmd->dst_pa = (uint64_t)(uintptr_t)bounce_page;
    cmd->len = (uint32_t)len;
    cmd->value = value;

    result = user_accel_copyback_result(
        accel_submit_sync_timeout(cmd, timeout_ticks)
    );
    if (result == USER_ACCEL_OK) {
        if (copy_to_user_task(task, (void *)user_dst, bounce_page, len) !=
            USERCOPY_OK) {
            result = USER_ACCEL_ERR_INVALID;
        }
    }

    page_free(bounce_page);
    page_free(cmd);
    return result;
}

trap_frame_t *user_accel_syscall_memset(trap_frame_t *frame)
{
    if (frame == NULL) {
        PANIC("null user accelerator syscall frame");
    }

    user_task_t *task = thread_current_user_task_for_frame(frame);
    if (task == NULL) {
        PANIC("scheduled user task required for accelerator syscall");
    }

    const int result = user_accel_memset_impl(
        task,
        (uintptr_t)frame->a0,
        frame->a1,
        (uint32_t)frame->a2,
        frame->a3
    );

    frame->mepc += 4;
    frame->a0 = (uint64_t)(int64_t)result;
    console_write("user: accel memset\n");
    return frame;
}
