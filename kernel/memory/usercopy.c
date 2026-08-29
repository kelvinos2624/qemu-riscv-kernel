#include "arch/riscv64/csr.h"
#include "arch/riscv64/irq.h"
#include "core/thread.h"
#include "memory/paging.h"
#include "memory/user_space.h"
#include "memory/usercopy.h"
#include "memory/vm.h"

#include <stdint.h>

extern char riscv_usercopy_copy_start[];
extern char riscv_usercopy_copy_end[];
extern char riscv_usercopy_copy_fixup[];
extern int riscv_usercopy_copy_bytes(void *dst, const void *src, size_t len);

static int usercopy_range_end(uintptr_t start, size_t len, uintptr_t *end)
{
    if (end == NULL) {
        return USERCOPY_ERR_INVALID;
    }

    if (len == 0) {
        *end = start;
        return USERCOPY_OK;
    }

    const uintptr_t range_end = start + len;
    if (range_end < start) {
        return USERCOPY_ERR_INVALID;
    }

    *end = range_end;
    return USERCOPY_OK;
}

static int usercopy_validate_range(uintptr_t user_va, size_t len, int write)
{
    uintptr_t end;
    if (usercopy_range_end(user_va, len, &end) != USERCOPY_OK) {
        return USERCOPY_ERR_INVALID;
    }

    if (len == 0) {
        return USERCOPY_OK;
    }

    if (!user_space_va_range_is_valid(user_va, len)) {
        return USERCOPY_ERR_INVALID;
    }

    const uintptr_t first_page = user_va & ~PAGE_MASK;
    const uintptr_t last_page = (end - 1u) & ~PAGE_MASK;
    const uint64_t needed = write ? VM_PTE_W : VM_PTE_R;

    for (uintptr_t page = first_page;; page += PAGE_SIZE) {
        uintptr_t pa;
        uint64_t flags;
        if (vm_get_mapping(paging_kernel_space(), page, &pa, &flags) != VM_OK) {
            return USERCOPY_ERR_INVALID;
        }

        (void)pa;
        if ((flags & VM_PTE_U) == 0 || (flags & needed) == 0) {
            return USERCOPY_ERR_INVALID;
        }

        if (page == last_page) {
            break;
        }
    }

    return USERCOPY_OK;
}

static void usercopy_restore_sstatus(uint64_t saved_sstatus)
{
    if ((saved_sstatus & SSTATUS_SUM) != 0) {
        csr_set_sstatus(SSTATUS_SUM);
    } else {
        csr_clear_sstatus(SSTATUS_SUM);
    }
}

static int probed_copy_bytes(
    void *dst,
    const void *src,
    size_t len,
    uintptr_t user_start,
    uintptr_t user_end,
    uint64_t fault_cause
)
{
    irq_state_t irq_state = irq_save();
    const uint64_t saved_sstatus = csr_read_sstatus();

    if (thread_usercopy_probe_begin(
            (uintptr_t)riscv_usercopy_copy_start,
            (uintptr_t)riscv_usercopy_copy_end,
            (uintptr_t)riscv_usercopy_copy_fixup,
            user_start,
            user_end,
            fault_cause
        ) < 0) {
        usercopy_restore_sstatus(saved_sstatus);
        irq_restore(irq_state);
        return USERCOPY_ERR_INVALID;
    }

    csr_set_sstatus(SSTATUS_SUM);
    const int result = riscv_usercopy_copy_bytes(dst, src, len);
    thread_usercopy_probe_end();
    usercopy_restore_sstatus(saved_sstatus);
    irq_restore(irq_state);
    return result;
}

int copy_from_user(void *dst, const void *user_src, size_t len)
{
    if (len == 0) {
        return USERCOPY_OK;
    }

    if (dst == NULL || user_src == NULL) {
        return USERCOPY_ERR_INVALID;
    }

    const uintptr_t user_start = (uintptr_t)user_src;
    uintptr_t user_end;
    if (usercopy_range_end(user_start, len, &user_end) != USERCOPY_OK) {
        return USERCOPY_ERR_INVALID;
    }

    int result = usercopy_validate_range(user_start, len, 0);
    if (result != USERCOPY_OK) {
        return result;
    }

    return probed_copy_bytes(
        dst,
        user_src,
        len,
        user_start,
        user_end,
        MCAUSE_LOAD_PAGE_FAULT
    );
}

int copy_to_user(void *user_dst, const void *src, size_t len)
{
    if (len == 0) {
        return USERCOPY_OK;
    }

    if (user_dst == NULL || src == NULL) {
        return USERCOPY_ERR_INVALID;
    }

    const uintptr_t user_start = (uintptr_t)user_dst;
    uintptr_t user_end;
    if (usercopy_range_end(user_start, len, &user_end) != USERCOPY_OK) {
        return USERCOPY_ERR_INVALID;
    }

    int result = usercopy_validate_range(user_start, len, 1);
    if (result != USERCOPY_OK) {
        return result;
    }

    return probed_copy_bytes(
        user_dst,
        src,
        len,
        user_start,
        user_end,
        MCAUSE_STORE_PAGE_FAULT
    );
}

int usercopy_recoverable_fault_selftest(void)
{
    uint8_t sink;
    const uintptr_t user_start = USER_SPACE_STACK_BASE;
    const uintptr_t user_end = user_start + 1u;

    return probed_copy_bytes(
        &sink,
        (const void *)user_start,
        sizeof(sink),
        user_start,
        user_end,
        MCAUSE_LOAD_PAGE_FAULT
    );
}
