#ifndef KERNEL_DRIVERS_ACCEL_H
#define KERNEL_DRIVERS_ACCEL_H

#include <stdint.h>

#include "drivers/device.h"

#define ACCEL_DEVICE_NAME "sim-accel0"
#define ACCEL_DEVICE_COMPATIBLE "qemu-rtos,sim-accel"
#define ACCEL_DEVICE_IRQ ((irq_t)2)
#define ACCEL_DEVICE_ID_VALUE ((uint32_t)0xacc35001u)

#define ACCEL_REG_ID ((uintptr_t)0x00)
#define ACCEL_REG_STATUS ((uintptr_t)0x04)
#define ACCEL_REG_CONTROL ((uintptr_t)0x08)
#define ACCEL_REG_IRQ_STATUS ((uintptr_t)0x0c)
#define ACCEL_REG_IRQ_ACK ((uintptr_t)0x10)
#define ACCEL_REG_CMD_BASE ((uintptr_t)0x18)
#define ACCEL_MMIO_SIZE ((uintptr_t)0x20)

#define ACCEL_STATUS_IDLE (1u << 0)
#define ACCEL_STATUS_BUSY (1u << 1)
#define ACCEL_STATUS_DONE (1u << 2)
#define ACCEL_STATUS_ERROR (1u << 3)

#define ACCEL_CONTROL_START (1u << 0)
#define ACCEL_CONTROL_RESET (1u << 1)

#define ACCEL_IRQ_DONE (1u << 0)
#define ACCEL_IRQ_ERROR (1u << 1)

#define ACCEL_OK 0
#define ACCEL_ERR_NO_DEVICE (-1)
#define ACCEL_ERR_INVALID (-2)
#define ACCEL_ERR_BUSY (-3)
#define ACCEL_ERR_IO (-4)

extern const driver_t accel_driver;

int accel_reset(void);
int accel_start_selftest(void);
int accel_get_status(uint32_t *status_out);
int accel_get_irq_status(uint32_t *irq_status_out);
int accel_ack_irq(uint32_t mask);
int accel_write_control_raw(uint32_t control);

#endif
