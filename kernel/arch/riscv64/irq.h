#ifndef KERNEL_ARCH_RISCV64_IRQ_H
#define KERNEL_ARCH_RISCV64_IRQ_H

#include "arch/riscv64/csr.h"

#include <stdint.h>

typedef uint64_t irq_state_t;

static inline irq_state_t irq_save(void)
{
    const irq_state_t state = csr_read_mstatus();
    csr_clear_mstatus(MSTATUS_MIE);
    return state;
}

static inline void irq_restore(irq_state_t state)
{
    if ((state & MSTATUS_MIE) != 0) {
        csr_set_mstatus(MSTATUS_MIE);
    } else {
        csr_clear_mstatus(MSTATUS_MIE);
    }
}

static inline void irq_enable(void)
{
    csr_set_mstatus(MSTATUS_MIE);
}

static inline void irq_disable(void)
{
    csr_clear_mstatus(MSTATUS_MIE);
}

#endif
