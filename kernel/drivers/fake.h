#ifndef KERNEL_DRIVERS_FAKE_H
#define KERNEL_DRIVERS_FAKE_H

#include <stdint.h>

#include "drivers/device.h"

#define FAKE_DEVICE_NAME "fake-mmio0"
#define FAKE_DEVICE_COMPATIBLE "qemu-rtos,fake-mmio"
#define FAKE_DEVICE_IRQ ((irq_t)1)
#define FAKE_DEVICE_ID_VALUE ((uint32_t)0x5152544fu)

#define FAKE_REG_ID ((uintptr_t)0x00)
#define FAKE_REG_SCRATCH ((uintptr_t)0x04)
#define FAKE_MMIO_SIZE ((uintptr_t)0x08)

extern const driver_t fake_driver;

#endif
