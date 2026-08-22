#include "core/kernel.h"
#include "core/thread.h"
#include "core/trap.h"
#include "drivers/timer.h"

extern char __kernel_start[];
extern char __kernel_end[];
extern char __stack_top[];

static void demo_thread_a(void *arg)
{
    (void)arg;

    console_write("thread: A0\n");
    thread_yield();
    console_write("thread: A1\n");
    thread_yield();
    console_write("thread: A2\n");
    thread_exit();
}

static void demo_thread_b(void *arg)
{
    (void)arg;

    console_write("thread: B0\n");
    thread_yield();
    console_write("thread: B1\n");
    thread_yield();
    console_write("thread: B2\n");
    console_write("milestone 3: cooperative kernel threads\n");
    thread_exit();
}

void kmain(void)
{
    console_write("\n");
    console_write("qemu-rtos: booting RISC-V kernel\n");
    console_write("kernel_start=");
    console_write_hex64((uint64_t)(uintptr_t)__kernel_start);
    console_write(" kernel_end=");
    console_write_hex64((uint64_t)(uintptr_t)__kernel_end);
    console_write(" stack_top=");
    console_write_hex64((uint64_t)(uintptr_t)__stack_top);
    console_write("\n");
    console_write("milestone 1: boot, stack, bss, uart console\n");

    trap_init();
    trap_selftest();
    console_write("milestone 2: trap vector setup\n");

    timer_init();
    while (timer_ticks() < 3) {
        __asm__ volatile("wfi");
    }
    console_write("timer: observed ");
    console_write_hex64(timer_ticks());
    console_write(" ticks\n");
    console_write("milestone 2: timer interrupt setup\n");

    thread_init();
    if (thread_create("demo-a", demo_thread_a, NULL) < 0) {
        PANIC("failed to create demo-a");
    }
    if (thread_create("demo-b", demo_thread_b, NULL) < 0) {
        PANIC("failed to create demo-b");
    }
    thread_start();
}
