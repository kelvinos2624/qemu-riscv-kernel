#include "core/kernel.h"
#include "core/sync.h"
#include "core/thread.h"
#include "core/trap.h"
#include "drivers/timer.h"

extern char __kernel_start[];
extern char __kernel_end[];
extern char __stack_top[];

static mutex_t demo_mutex;
static volatile int demo_shared_counter;

static void demo_thread_a(void *arg)
{
    (void)arg;

    console_write("thread: mutex-a locking\n");
    mutex_lock(&demo_mutex);
    console_write("thread: mutex-a acquired\n");
    demo_shared_counter++;
    thread_yield();
    console_write("thread: mutex-a unlocking\n");
    mutex_unlock(&demo_mutex);
    thread_exit();
}

static void demo_thread_b(void *arg)
{
    (void)arg;

    console_write("thread: mutex-b waiting\n");
    mutex_lock(&demo_mutex);
    console_write("thread: mutex-b acquired\n");
    demo_shared_counter++;
    mutex_unlock(&demo_mutex);
    console_write("milestone 8: mutexes\n");
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
    mutex_init(&demo_mutex, "demo-mutex");
    demo_shared_counter = 0;
    if (thread_create("demo-a", demo_thread_a, NULL) < 0) {
        PANIC("failed to create demo-a");
    }
    if (thread_create("demo-b", demo_thread_b, NULL) < 0) {
        PANIC("failed to create demo-b");
    }
    thread_start();
}
