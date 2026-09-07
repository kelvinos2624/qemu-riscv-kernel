#include "user/runtime.h"
#include "user_abi.h"

int user_main(void)
{
    for (uint32_t i = 0; i < USER_BENCH_SYSCALL_ITERATIONS; i++) {
        if (user_noop() != USER_SYSCALL_OK) {
            return 1;
        }
    }

    return 0;
}
