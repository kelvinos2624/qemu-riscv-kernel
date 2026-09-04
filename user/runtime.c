#include "user/runtime.h"
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
