#include "user/runtime.h"
#include "user_accel.h"
#include "user_abi.h"

int user_main(void)
{
    uint8_t buffer[USER_BENCH_ACCEL_LEN];

    for (uint32_t iteration = 0; iteration < USER_BENCH_ACCEL_ITERATIONS;
         iteration++) {
        const uint8_t value = (uint8_t)(0x31u + iteration);

        for (uint32_t i = 0; i < sizeof(buffer); i++) {
            buffer[i] = 0;
        }

        if (user_accel_memset(buffer, sizeof(buffer), value, 100u) !=
            USER_ACCEL_OK) {
            return 1;
        }

        for (uint32_t i = 0; i < sizeof(buffer); i++) {
            if (buffer[i] != value) {
                return 2;
            }
        }
    }

    return 0;
}
