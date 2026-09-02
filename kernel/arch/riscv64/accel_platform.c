#include "arch/riscv64/accel_platform.h"
#include "drivers/accel.h"

typedef struct accel_mmio_region {
    uint32_t id;
    uint32_t status;
    uint32_t control;
    uint32_t irq_status;
    uint32_t irq_ack;
} accel_mmio_region_t;

static accel_mmio_region_t accel_mmio = {
    .id = ACCEL_DEVICE_ID_VALUE,
    .status = ACCEL_STATUS_IDLE,
    .control = 0,
    .irq_status = 0,
    .irq_ack = 0,
};

static const device_resource_t accel_resource = {
    .name = ACCEL_DEVICE_NAME,
    .compatible = ACCEL_DEVICE_COMPATIBLE,
    .mmio_base = (uintptr_t)&accel_mmio,
    .mmio_size = ACCEL_MMIO_SIZE,
    .irq = ACCEL_DEVICE_IRQ,
};

static void accel_enter_error(void)
{
    accel_mmio.status = ACCEL_STATUS_ERROR;
    accel_mmio.irq_status = ACCEL_IRQ_ERROR;
}

static void accel_reset_state(void)
{
    accel_mmio.status = ACCEL_STATUS_IDLE;
    accel_mmio.irq_status = 0;
    accel_mmio.irq_ack = 0;
}

static void accel_start(void)
{
    if (accel_mmio.status != ACCEL_STATUS_IDLE) {
        accel_enter_error();
        return;
    }

    accel_mmio.status = ACCEL_STATUS_BUSY;
    accel_mmio.status = ACCEL_STATUS_DONE;
    accel_mmio.irq_status = ACCEL_IRQ_DONE;
}

const device_resource_t *platform_accel_resource(void)
{
    return &accel_resource;
}

void platform_accel_step(void)
{
    const uint32_t control = accel_mmio.control;
    const uint32_t irq_ack = accel_mmio.irq_ack;

    accel_mmio.control = 0;
    accel_mmio.irq_ack = 0;

    if ((control & ACCEL_CONTROL_RESET) != 0) {
        accel_reset_state();
        return;
    }

    if ((control & ACCEL_CONTROL_START) != 0) {
        accel_start();
    }

    accel_mmio.irq_status &= ~irq_ack;
}
