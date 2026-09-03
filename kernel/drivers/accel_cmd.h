#ifndef KERNEL_DRIVERS_ACCEL_CMD_H
#define KERNEL_DRIVERS_ACCEL_CMD_H

#include <stdint.h>

#include "drivers/accel.h"

#define ACCEL_CMD_OP_MEMSET 1u

#define ACCEL_CMD_STATUS_PENDING 0u
#define ACCEL_CMD_STATUS_OK 1u
#define ACCEL_CMD_STATUS_INVALID 2u
#define ACCEL_CMD_STATUS_ERROR 3u
#define ACCEL_CMD_STATUS_REJECTED 4u

#define ACCEL_CMD_ALIGNMENT 8u

typedef struct accel_cmd {
    uint32_t op;
    uint32_t flags;
    uint64_t dst_pa;
    uint32_t len;
    uint32_t value;
    uint32_t status;
    uint32_t reserved;
} accel_cmd_t;

int accel_submit_sync(accel_cmd_t *cmd);

#endif
