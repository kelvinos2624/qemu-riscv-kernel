#ifndef USER_ACCEL_H
#define USER_ACCEL_H

#include <stdint.h>

#define USER_ACCEL_OK 0
#define USER_ACCEL_ERR_NO_DEVICE (-1)
#define USER_ACCEL_ERR_INVALID (-2)
#define USER_ACCEL_ERR_BUSY (-3)
#define USER_ACCEL_ERR_IO (-4)
#define USER_ACCEL_ERR_TIMEOUT (-5)
#define USER_ACCEL_ERR_NO_MEMORY (-6)

#define USER_ACCEL_MEMSET_MAX_LEN 4096u

int user_accel_memset(
    void *dst,
    uint32_t len,
    uint32_t value,
    uint64_t timeout_ticks
);

#endif
