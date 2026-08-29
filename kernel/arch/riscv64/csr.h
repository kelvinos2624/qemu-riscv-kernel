#ifndef KERNEL_ARCH_RISCV64_CSR_H
#define KERNEL_ARCH_RISCV64_CSR_H

#include <stdint.h>

#define MCAUSE_INTERRUPT ((uint64_t)1 << 63)
#define MCAUSE_CODE_MASK (~MCAUSE_INTERRUPT)

#define MSTATUS_MIE (1u << 3)
#define MSTATUS_MPIE (1u << 7)
#define MSTATUS_MPP_SHIFT 11u
#define MSTATUS_MPP_MASK ((uint64_t)3 << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_S ((uint64_t)1 << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_M ((uint64_t)3 << 11)
#define SSTATUS_SIE (1u << 1)
#define SSTATUS_SPIE (1u << 5)
#define SSTATUS_SPP (1u << 8)
#define MIE_MTIE (1u << 7)
#define SIE_STIE (1u << 5)
#define MIP_STIP (1u << 5)
#define SATP_MODE_SV39 ((uint64_t)8 << 60)
#define MCOUNTEREN_TM (1u << 1)
#define PMP_R (1u << 0)
#define PMP_W (1u << 1)
#define PMP_X (1u << 2)
#define PMP_A_NAPOT (3u << 3)

#define MCAUSE_BREAKPOINT 3
#define MCAUSE_ILLEGAL_INSTRUCTION 2
#define MCAUSE_INSTRUCTION_ACCESS_FAULT 1
#define MCAUSE_LOAD_ACCESS_FAULT 5
#define MCAUSE_STORE_ACCESS_FAULT 7
#define MCAUSE_ECALL_U_MODE 8
#define MCAUSE_ECALL_S_MODE 9
#define MCAUSE_INSTRUCTION_PAGE_FAULT 12
#define MCAUSE_LOAD_PAGE_FAULT 13
#define MCAUSE_STORE_PAGE_FAULT 15
#define MCAUSE_MACHINE_TIMER_INTERRUPT 7
#define MCAUSE_ECALL_M_MODE 11
#define SCAUSE_SUPERVISOR_TIMER_INTERRUPT 5

static inline uint64_t csr_read_mstatus(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mstatus" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_sstatus(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, sstatus" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_mcause(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mcause" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_scause(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, scause" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_mepc(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mepc" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_sepc(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, sepc" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_mie(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mie" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_sie(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, sie" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_mtval(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mtval" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_stval(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, stval" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_mtvec(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mtvec" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_stvec(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, stvec" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_satp(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, satp" : "=r"(value));
    return value;
}

static inline void csr_write_mepc(uint64_t value)
{
    __asm__ volatile("csrw mepc, %0" : : "r"(value) : "memory");
}

static inline void csr_write_mstatus(uint64_t value)
{
    __asm__ volatile("csrw mstatus, %0" : : "r"(value) : "memory");
}

static inline void csr_write_sepc(uint64_t value)
{
    __asm__ volatile("csrw sepc, %0" : : "r"(value) : "memory");
}

static inline void csr_write_satp(uint64_t value)
{
    __asm__ volatile("csrw satp, %0" : : "r"(value) : "memory");
}

static inline void csr_set_mstatus(uint64_t mask)
{
    __asm__ volatile("csrs mstatus, %0" : : "r"(mask) : "memory");
}

static inline void csr_set_sstatus(uint64_t mask)
{
    __asm__ volatile("csrs sstatus, %0" : : "r"(mask) : "memory");
}

static inline void csr_clear_mstatus(uint64_t mask)
{
    __asm__ volatile("csrc mstatus, %0" : : "r"(mask) : "memory");
}

static inline void csr_clear_sstatus(uint64_t mask)
{
    __asm__ volatile("csrc sstatus, %0" : : "r"(mask) : "memory");
}

static inline void csr_set_mie(uint64_t mask)
{
    __asm__ volatile("csrs mie, %0" : : "r"(mask) : "memory");
}

static inline void csr_set_sie(uint64_t mask)
{
    __asm__ volatile("csrs sie, %0" : : "r"(mask) : "memory");
}

static inline void csr_clear_mie(uint64_t mask)
{
    __asm__ volatile("csrc mie, %0" : : "r"(mask) : "memory");
}

static inline void csr_clear_sie(uint64_t mask)
{
    __asm__ volatile("csrc sie, %0" : : "r"(mask) : "memory");
}

static inline void csr_write_mtvec(uint64_t value)
{
    __asm__ volatile("csrw mtvec, %0" : : "r"(value) : "memory");
}

static inline void csr_write_stvec(uint64_t value)
{
    __asm__ volatile("csrw stvec, %0" : : "r"(value) : "memory");
}

static inline void csr_write_medeleg(uint64_t value)
{
    __asm__ volatile("csrw medeleg, %0" : : "r"(value) : "memory");
}

static inline void csr_write_mideleg(uint64_t value)
{
    __asm__ volatile("csrw mideleg, %0" : : "r"(value) : "memory");
}

static inline void csr_write_mcounteren(uint64_t value)
{
    __asm__ volatile("csrw mcounteren, %0" : : "r"(value) : "memory");
}

static inline void csr_write_pmpaddr0(uint64_t value)
{
    __asm__ volatile("csrw pmpaddr0, %0" : : "r"(value) : "memory");
}

static inline void csr_write_pmpcfg0(uint64_t value)
{
    __asm__ volatile("csrw pmpcfg0, %0" : : "r"(value) : "memory");
}

static inline void csr_set_mip(uint64_t mask)
{
    __asm__ volatile("csrs mip, %0" : : "r"(mask) : "memory");
}

static inline void csr_clear_mip(uint64_t mask)
{
    __asm__ volatile("csrc mip, %0" : : "r"(mask) : "memory");
}

static inline void sfence_vma(void)
{
    __asm__ volatile("sfence.vma" : : : "memory");
}

#endif
