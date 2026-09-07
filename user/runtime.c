#include "user/runtime.h"
#include "user_accel.h"
#include "user_abi.h"

static uint64_t user_syscall0(uint64_t nr)
{
    register uint64_t a0 __asm__("a0") = 0;
    register uint64_t a7 __asm__("a7") = nr;

    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return a0;
}

static uint64_t user_syscall1(uint64_t nr, uint64_t arg0)
{
    register uint64_t a0 __asm__("a0") = arg0;
    register uint64_t a7 __asm__("a7") = nr;

    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    return a0;
}

static uint64_t user_syscall4(
    uint64_t nr,
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3
)
{
    register uint64_t a0 __asm__("a0") = arg0;
    register uint64_t a1 __asm__("a1") = arg1;
    register uint64_t a2 __asm__("a2") = arg2;
    register uint64_t a3 __asm__("a3") = arg3;
    register uint64_t a7 __asm__("a7") = nr;

    __asm__ volatile(
        "ecall"
        : "+r"(a0)
        : "r"(a1), "r"(a2), "r"(a3), "r"(a7)
        : "memory"
    );
    return a0;
}

void user_exit(uint64_t code)
{
    (void)user_syscall1(USER_SYSCALL_EXIT, code);

    for (;;) {
        __asm__ volatile("" : : : "memory");
    }
}

uint64_t user_yield(void)
{
    return user_syscall0(USER_SYSCALL_YIELD);
}

uint64_t user_sleep(uint64_t ticks)
{
    return user_syscall1(USER_SYSCALL_SLEEP, ticks);
}

int user_accel_memset(
    void *dst,
    uint32_t len,
    uint32_t value,
    uint64_t timeout_ticks
)
{
    const uint64_t result = user_syscall4(
        USER_SYSCALL_ACCEL_MEMSET,
        (uint64_t)(uintptr_t)dst,
        (uint64_t)len,
        (uint64_t)value,
        timeout_ticks
    );

    return (int)(int64_t)result;
}
