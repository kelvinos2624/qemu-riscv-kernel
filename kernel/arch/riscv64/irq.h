#ifndef KERNEL_ARCH_RISCV64_IRQ_H
#define KERNEL_ARCH_RISCV64_IRQ_H

#include "arch/riscv64/csr.h"

#include <stdint.h>

typedef uint64_t irq_state_t;

static inline irq_state_t irq_save(void)
{
    const irq_state_t state = csr_read_sstatus();
    csr_clear_sstatus(SSTATUS_SIE);
    return state;
}

static inline void irq_restore(irq_state_t state)
{
    if ((state & SSTATUS_SIE) != 0) {
        csr_set_sstatus(SSTATUS_SIE);
    } else {
        csr_clear_sstatus(SSTATUS_SIE);
    }
}

static inline void irq_enable(void)
{
    csr_set_sstatus(SSTATUS_SIE);
}

static inline void irq_disable(void)
{
    csr_clear_sstatus(SSTATUS_SIE);
}

#endif
