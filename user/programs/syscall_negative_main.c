#include "user_accel.h"
#include "user_abi.h"

#include <stdint.h>

#define USER_NEG_UNKNOWN_SYSCALL 0xffffu
#define USER_NEG_CODE_BASE ((uintptr_t)0x0000000000001000ull)
#define USER_NEG_UNMAPPED_VA ((uintptr_t)0x0000000000002000ull)
#define USER_NEG_WRAP_VA ((uintptr_t)(UINTPTR_MAX - 3u))

static uint64_t user_negative_syscall0(uint64_t nr)
{
    register uint64_t a0 __asm__("a0") = 0;
    register uint64_t a7 __asm__("a7") = nr;

    __asm__ volatile(
        "ecall"
        : "+r"(a0)
        : "r"(a7)
        : "memory"
    );
    return a0;
}

static int expect_result(int actual, int expected, int code)
{
    return actual == expected ? 0 : code;
}

int user_main(void)
{
    uint8_t buffer[64];

    for (uint32_t i = 0; i < sizeof(buffer); i++) {
        buffer[i] = 0;
    }

    int result = expect_result(
        (int)(int64_t)user_negative_syscall0(USER_NEG_UNKNOWN_SYSCALL),
        USER_SYSCALL_ERR_UNKNOWN,
        1
    );
    if (result != 0) {
        return result;
    }

    result = expect_result(
        user_accel_memset(0, sizeof(buffer), 0x11u, 100u),
        USER_ACCEL_ERR_INVALID,
        2
    );
    if (result != 0) {
        return result;
    }

    result = expect_result(
        user_accel_memset(buffer, 0, 0x22u, 100u),
        USER_ACCEL_ERR_INVALID,
        3
    );
    if (result != 0) {
        return result;
    }

    result = expect_result(
        user_accel_memset(buffer, USER_ACCEL_MEMSET_MAX_LEN + 1u, 0x33u, 100u),
        USER_ACCEL_ERR_INVALID,
        4
    );
    if (result != 0) {
        return result;
    }

    result = expect_result(
        user_accel_memset((void *)USER_NEG_UNMAPPED_VA, 1u, 0x44u, 100u),
        USER_ACCEL_ERR_INVALID,
        5
    );
    if (result != 0) {
        return result;
    }

    result = expect_result(
        user_accel_memset((void *)USER_NEG_CODE_BASE, 1u, 0x55u, 100u),
        USER_ACCEL_ERR_INVALID,
        6
    );
    if (result != 0) {
        return result;
    }

    result = expect_result(
        user_accel_memset((void *)USER_NEG_WRAP_VA, 8u, 0x66u, 100u),
        USER_ACCEL_ERR_INVALID,
        7
    );
    if (result != 0) {
        return result;
    }

    result = expect_result(
        user_accel_memset(buffer, sizeof(buffer), 0xa5u, 1u),
        USER_ACCEL_ERR_TIMEOUT,
        8
    );
    if (result != 0) {
        return result;
    }

    for (uint32_t i = 0; i < sizeof(buffer); i++) {
        if (buffer[i] != 0) {
            return 9;
        }
    }

    result = expect_result(
        user_accel_memset(buffer, sizeof(buffer), 0x5au, 100u),
        USER_ACCEL_OK,
        10
    );
    if (result != 0) {
        return result;
    }

    for (uint32_t i = 0; i < sizeof(buffer); i++) {
        if (buffer[i] != 0x5au) {
            return 11;
        }
    }

    return 0;
}
