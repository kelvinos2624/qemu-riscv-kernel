#include "arch/riscv64/csr.h"
#include "core/kernel.h"
#include "drivers/uart.h"
#include "drivers/timer.h"
#include "memory/page_alloc.h"
#include "memory/paging.h"
#include "memory/vm.h"

#define CLINT_MMIO_BASE ((uintptr_t)0x02000000)
#define CLINT_MMIO_SIZE ((uintptr_t)0x00010000)

extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __kernel_end[];
extern char __ram_end[];

static vm_space_t kernel_space;

static uintptr_t align_down(uintptr_t value, uintptr_t alignment)
{
    return value & ~(alignment - 1u);
}

static uintptr_t align_up(uintptr_t value, uintptr_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static void map_range(
    uintptr_t start,
    uintptr_t end,
    uint64_t flags,
    const char *name
)
{
    const uintptr_t aligned_start = align_down(start, PAGE_SIZE);
    const uintptr_t aligned_end = align_up(end, PAGE_SIZE);

    for (uintptr_t addr = aligned_start; addr < aligned_end; addr += PAGE_SIZE) {
        const int result = vm_map_page(&kernel_space, addr, addr, flags);
        if (result != VM_OK) {
            console_write("paging: map failed for ");
            console_write(name);
            console_write(" addr=");
            console_write_hex64(addr);
            console_write(" result=");
            console_write_hex64((uint64_t)(int64_t)result);
            console_write("\n");
            PANIC("kernel identity map failed");
        }
    }
}

uint64_t paging_init_kernel(void)
{
    if (vm_space_init(&kernel_space) != VM_OK) {
        PANIC("kernel page table init failed");
    }

    const uint64_t text_flags = VM_PTE_V | VM_PTE_R | VM_PTE_X | VM_PTE_A;
    const uint64_t ro_flags = VM_PTE_V | VM_PTE_R | VM_PTE_A;
    const uint64_t rw_flags =
        VM_PTE_V | VM_PTE_R | VM_PTE_W | VM_PTE_A | VM_PTE_D;

    map_range(
        (uintptr_t)__text_start,
        (uintptr_t)__text_end,
        text_flags,
        "text"
    );
    map_range(
        (uintptr_t)__rodata_start,
        (uintptr_t)__rodata_end,
        ro_flags,
        "rodata"
    );
    map_range(
        (uintptr_t)__data_start,
        (uintptr_t)__kernel_end,
        rw_flags,
        "kernel-data"
    );
    map_range(page_managed_start(), (uintptr_t)__ram_end, rw_flags, "ram");
    map_range(UART0_BASE, UART0_BASE + PAGE_SIZE, rw_flags, "uart");
    map_range(CLINT_MMIO_BASE, CLINT_MMIO_BASE + CLINT_MMIO_SIZE, rw_flags, "clint");

    return SATP_MODE_SV39 | ((uint64_t)(uintptr_t)kernel_space.root >> 12);
}

vm_space_t *paging_kernel_space(void)
{
    return &kernel_space;
}
