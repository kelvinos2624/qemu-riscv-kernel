#include "arch/riscv64/csr.h"
#include "arch/riscv64/machine.h"
#include "core/kernel.h"
#include "core/scenario.h"
#include "core/trap.h"
#include "drivers/device.h"
#include "drivers/timer.h"
#include "memory/heap.h"
#include "memory/paging.h"
#include "memory/page_alloc.h"

extern char __kernel_start[];
extern char __kernel_end[];
extern char __ram_end[];
extern char __stack_top[];

static void supervisor_main(void) __attribute__((noreturn));

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

    page_init((uintptr_t)__kernel_end, (uintptr_t)__ram_end);
    const uint64_t satp = paging_init_kernel();
    machine_init();
    machine_enter_supervisor(satp, supervisor_main);
}

static void supervisor_main(void)
{
    console_write("paging: satp=");
    console_write_hex64(csr_read_satp());
    console_write("\n");
    console_write("milestone 13: kernel paging\n");

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

    heap_init();
    device_init();
    (void)driver_probe_all();
    scenario_run();
}
