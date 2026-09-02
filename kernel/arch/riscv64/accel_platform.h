#ifndef KERNEL_ARCH_RISCV64_ACCEL_PLATFORM_H
#define KERNEL_ARCH_RISCV64_ACCEL_PLATFORM_H

#include "drivers/device.h"

const device_resource_t *platform_accel_resource(void);
void platform_accel_step(void);

#endif
