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

    console_write("thread: hog start\n");
    const uint64_t start_ticks = timer_ticks();
    while (timer_ticks() - start_ticks < THREAD_QUANTUM_TICKS + 2u) {
        __asm__ volatile("" ::: "memory");
    }
    console_write("thread: hog done\n");
    console_write("milestone 5: timer preemption\n");
    thread_exit();
}

static void demo_thread_b(void *arg)
{
    (void)arg;

    console_write("thread: peer ran without yield\n");
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
