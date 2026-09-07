#include "user_accel.h"

int user_main(void)
{
    uint8_t buffer[64];

    for (uint32_t i = 0; i < sizeof(buffer); i++) {
        buffer[i] = 0;
    }

    const int result = user_accel_memset(buffer, sizeof(buffer), 0x5au, 100u);
    if (result != USER_ACCEL_OK) {
        return 1;
    }

    for (uint32_t i = 0; i < sizeof(buffer); i++) {
        if (buffer[i] != 0x5au) {
            return 2;
        }
    }

    return 0;
}
