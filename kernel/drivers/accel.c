#include "arch/riscv64/accel_platform.h"
#include "drivers/accel.h"
#include "drivers/mmio.h"

static device_t *accel_device(void)
{
    device_t *dev = device_find_by_compatible(ACCEL_DEVICE_COMPATIBLE);
    if (dev == NULL || !device_is_bound(dev)) {
        return NULL;
    }

    return dev;
}

static int accel_probe(device_t *dev)
{
    const uint32_t id = mmio_read32(device_mmio_base(dev) + ACCEL_REG_ID);
    return id == ACCEL_DEVICE_ID_VALUE ? 0 : -1;
}

static void accel_irq_handler(device_t *dev)
{
    (void)dev;
}

const driver_t accel_driver = {
    .name = "sim-accel",
    .compatible = ACCEL_DEVICE_COMPATIBLE,
    .probe = accel_probe,
    .irq_handler = accel_irq_handler,
};

int accel_reset(void)
{
    return accel_write_control_raw(ACCEL_CONTROL_RESET);
}

int accel_start_selftest(void)
{
    return accel_write_control_raw(ACCEL_CONTROL_START);
}

int accel_get_status(uint32_t *status_out)
{
    if (status_out == NULL) {
        return ACCEL_ERR_INVALID;
    }

    device_t *dev = accel_device();
    if (dev == NULL) {
        return ACCEL_ERR_NO_DEVICE;
    }

    *status_out = mmio_read32(device_mmio_base(dev) + ACCEL_REG_STATUS);
    mmio_fence_after_device_read();
    return ACCEL_OK;
}

int accel_get_irq_status(uint32_t *irq_status_out)
{
    if (irq_status_out == NULL) {
        return ACCEL_ERR_INVALID;
    }

    device_t *dev = accel_device();
    if (dev == NULL) {
        return ACCEL_ERR_NO_DEVICE;
    }

    *irq_status_out = mmio_read32(device_mmio_base(dev) + ACCEL_REG_IRQ_STATUS);
    mmio_fence_after_device_read();
    return ACCEL_OK;
}

int accel_ack_irq(uint32_t mask)
{
    const uint32_t known_bits = ACCEL_IRQ_DONE | ACCEL_IRQ_ERROR;
    if ((mask & ~known_bits) != 0) {
        return ACCEL_ERR_INVALID;
    }

    device_t *dev = accel_device();
    if (dev == NULL) {
        return ACCEL_ERR_NO_DEVICE;
    }

    mmio_fence_before_device_write();
    mmio_write32(device_mmio_base(dev) + ACCEL_REG_IRQ_ACK, mask);
    platform_accel_step();
    return ACCEL_OK;
}

int accel_write_control_raw(uint32_t control)
{
    const uint32_t known_bits = ACCEL_CONTROL_START | ACCEL_CONTROL_RESET;
    if ((control & ~known_bits) != 0) {
        return ACCEL_ERR_INVALID;
    }

    device_t *dev = accel_device();
    if (dev == NULL) {
        return ACCEL_ERR_NO_DEVICE;
    }

    mmio_fence_before_device_write();
    mmio_write32(device_mmio_base(dev) + ACCEL_REG_CONTROL, control);
    platform_accel_step();
    return ACCEL_OK;
}
