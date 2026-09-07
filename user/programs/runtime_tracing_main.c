#include "user/runtime.h"
#include "user_accel.h"
#include "user_abi.h"

int user_main(void)
{
    uint8_t buffer[64];

    for (uint32_t i = 0; i < sizeof(buffer); i++) {
        buffer[i] = 0;
    }

    if (user_yield() != USER_SYSCALL_OK) {
        return 1;
    }

    if (user_sleep(2) != USER_SYSCALL_OK) {
        return 2;
    }

    const int timeout_result =
        user_accel_memset(buffer, sizeof(buffer), 0xa5u, 1u);
    if (timeout_result != USER_ACCEL_ERR_TIMEOUT) {
        return 3;
    }

    for (uint32_t i = 0; i < sizeof(buffer); i++) {
        if (buffer[i] != 0) {
            return 4;
        }
    }

    const int result = user_accel_memset(buffer, sizeof(buffer), 0x3du, 100u);
    if (result != USER_ACCEL_OK) {
        return 5;
    }

    for (uint32_t i = 0; i < sizeof(buffer); i++) {
        if (buffer[i] != 0x3du) {
            return 6;
        }
    }

    return 0;
}
