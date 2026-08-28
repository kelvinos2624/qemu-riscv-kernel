#include "arch/riscv64/csr.h"
#include "arch/riscv64/machine.h"
#include "core/kernel.h"
#include "drivers/timer.h"

#define MACHINE_ECALL_SET_TIMER 1u
#define PMP_ALL_MEMORY_NAPOT UINT64_MAX

extern void machine_trap_entry(void);

static inline void mmio_write64(uintptr_t addr, uint64_t value)
{
    *(volatile uint64_t *)addr = value;
}

static void machine_write_mtimecmp(uint64_t value)
{
    mmio_write64(TIMER_CLINT_MTIMECMP, value);
}

static void machine_allow_supervisor_memory_access(void)
{
    csr_write_pmpaddr0(PMP_ALL_MEMORY_NAPOT);
    csr_write_pmpcfg0(PMP_R | PMP_W | PMP_X | PMP_A_NAPOT);
}

void machine_init(void)
{
    const uint64_t delegated_exceptions =
        (1ull << MCAUSE_INSTRUCTION_ACCESS_FAULT) |
        (1ull << MCAUSE_ILLEGAL_INSTRUCTION) |
        (1ull << MCAUSE_BREAKPOINT) |
        (1ull << MCAUSE_LOAD_ACCESS_FAULT) |
        (1ull << MCAUSE_STORE_ACCESS_FAULT) |
        (1ull << MCAUSE_INSTRUCTION_PAGE_FAULT) |
        (1ull << MCAUSE_LOAD_PAGE_FAULT) |
        (1ull << MCAUSE_STORE_PAGE_FAULT);

    machine_allow_supervisor_memory_access();
    machine_write_mtimecmp(UINT64_MAX);
    csr_write_mtvec((uint64_t)(uintptr_t)machine_trap_entry);
    csr_write_medeleg(delegated_exceptions);
    csr_write_mideleg(1ull << SCAUSE_SUPERVISOR_TIMER_INTERRUPT);
    csr_write_mcounteren(MCOUNTEREN_TM);
    csr_set_mie(MIE_MTIE);
}

void machine_enter_supervisor(uint64_t satp, void (*entry)(void))
{
    csr_write_satp(satp);
    sfence_vma();

    csr_write_mepc((uint64_t)(uintptr_t)entry);
    uint64_t status = csr_read_mstatus();
    status &= ~MSTATUS_MPP_MASK;
    status |= MSTATUS_MPP_S;
    csr_write_mstatus(status);

    __asm__ volatile("mret" : : : "memory");
    __builtin_unreachable();
}

int machine_call_set_timer(uint64_t deadline)
{
    register uint64_t arg0 __asm__("a0") = deadline;
    register uint64_t op __asm__("a7") = MACHINE_ECALL_SET_TIMER;
    __asm__ volatile("ecall" : "+r"(arg0) : "r"(op) : "memory");
    return (int)(intptr_t)arg0;
}

trap_frame_t *machine_trap_handle(trap_frame_t *frame)
{
    const uint64_t is_interrupt = frame->mcause & MCAUSE_INTERRUPT;
    const uint64_t cause = frame->mcause & MCAUSE_CODE_MASK;

    /*
     * M-mode is a platform shim only. Kernel policy stays in S-mode; see
     * DDR 21 before adding work to this handler.
     */
    if (!is_interrupt && cause == MCAUSE_ECALL_S_MODE) {
        if (frame->a7 == MACHINE_ECALL_SET_TIMER) {
            csr_clear_mip(MIP_STIP);
            machine_write_mtimecmp(frame->a0);
            frame->a0 = 0;
            frame->mepc += 4;
            return frame;
        }
    }

    if (is_interrupt && cause == MCAUSE_MACHINE_TIMER_INTERRUPT) {
        machine_write_mtimecmp(UINT64_MAX);
        csr_set_mip(MIP_STIP);
        return frame;
    }

    console_write("\nmachine: unhandled trap mcause=");
    console_write_hex64(frame->mcause);
    console_write(" mepc=");
    console_write_hex64(frame->mepc);
    console_write(" mtval=");
    console_write_hex64(frame->mtval);
    console_write("\n");
    PANIC("unhandled machine trap");
}
