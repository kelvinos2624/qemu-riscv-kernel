#ifndef KERNEL_ARCH_RISCV64_MACHINE_H
#define KERNEL_ARCH_RISCV64_MACHINE_H

#include "core/trap.h"

#include <stdint.h>

void machine_init(void);
void machine_enter_supervisor(uint64_t satp, void (*entry)(void)) __attribute__((noreturn));
int machine_call_set_timer(uint64_t deadline);
trap_frame_t *machine_trap_handle(trap_frame_t *frame);

#endif
