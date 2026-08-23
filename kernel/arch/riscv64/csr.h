#ifndef KERNEL_ARCH_RISCV64_CSR_H
#define KERNEL_ARCH_RISCV64_CSR_H

#include <stdint.h>

#define MCAUSE_INTERRUPT ((uint64_t)1 << 63)
#define MCAUSE_CODE_MASK (~MCAUSE_INTERRUPT)

#define MSTATUS_MIE (1u << 3)
#define MSTATUS_MPIE (1u << 7)
#define MSTATUS_MPP_M ((uint64_t)3 << 11)
#define MIE_MTIE (1u << 7)

#define MCAUSE_MACHINE_TIMER_INTERRUPT 7
#define MCAUSE_ECALL_M_MODE 11

static inline uint64_t csr_read_mstatus(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mstatus" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_mcause(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mcause" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_mepc(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mepc" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_mie(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mie" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_mtval(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mtval" : "=r"(value));
    return value;
}

static inline uint64_t csr_read_mtvec(void)
{
    uint64_t value;
    __asm__ volatile("csrr %0, mtvec" : "=r"(value));
    return value;
}

static inline void csr_write_mepc(uint64_t value)
{
    __asm__ volatile("csrw mepc, %0" : : "r"(value) : "memory");
}

static inline void csr_set_mstatus(uint64_t mask)
{
    __asm__ volatile("csrs mstatus, %0" : : "r"(mask) : "memory");
}

static inline void csr_clear_mstatus(uint64_t mask)
{
    __asm__ volatile("csrc mstatus, %0" : : "r"(mask) : "memory");
}

static inline void csr_set_mie(uint64_t mask)
{
    __asm__ volatile("csrs mie, %0" : : "r"(mask) : "memory");
}

static inline void csr_clear_mie(uint64_t mask)
{
    __asm__ volatile("csrc mie, %0" : : "r"(mask) : "memory");
}

static inline void csr_write_mtvec(uint64_t value)
{
    __asm__ volatile("csrw mtvec, %0" : : "r"(value) : "memory");
}

#endif
