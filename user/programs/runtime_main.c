#include "user/runtime.h"
#include "user_abi.h"

int user_main(void)
{
    if (user_yield() != USER_SYSCALL_OK) {
        return 1;
    }

    if (user_sleep(2) != USER_SYSCALL_OK) {
        return 2;
    }

    return 0;
}
