#include "arch/riscv64/accel_platform.h"
#include "drivers/accel.h"
#include "drivers/accel_cmd.h"
#include "drivers/mmio.h"
#include "memory/page_alloc.h"

_Static_assert(sizeof(accel_cmd_t) == 32u, "accelerator command descriptor size");

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

static int accel_cmd_pointer_is_safe(const accel_cmd_t *cmd)
{
    const uintptr_t addr = (uintptr_t)cmd;
    return cmd != NULL &&
        (addr & (ACCEL_CMD_ALIGNMENT - 1u)) == 0 &&
        page_range_is_managed_page_contained(addr, sizeof(*cmd));
}

static int accel_cmd_payload_is_valid(const accel_cmd_t *cmd)
{
    if (cmd->op != ACCEL_CMD_OP_MEMSET ||
        cmd->flags != 0 ||
        cmd->reserved != 0 ||
        cmd->len == 0) {
        return 0;
    }

    return page_range_is_managed_page_contained(
        (uintptr_t)cmd->dst_pa,
        (size_t)cmd->len);
}

static int accel_device_is_ready(device_t *dev)
{
    const uintptr_t base = device_mmio_base(dev);
    const uint32_t status = mmio_read32(base + ACCEL_REG_STATUS);
    const uint32_t irq_status = mmio_read32(base + ACCEL_REG_IRQ_STATUS);
    mmio_fence_after_device_read();

    return status == ACCEL_STATUS_IDLE && irq_status == 0;
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

int accel_submit_sync(accel_cmd_t *cmd)
{
    if (!accel_cmd_pointer_is_safe(cmd)) {
        return ACCEL_ERR_INVALID;
    }

    device_t *dev = accel_device();
    if (dev == NULL) {
        return ACCEL_ERR_NO_DEVICE;
    }

    if (!accel_cmd_payload_is_valid(cmd)) {
        cmd->status = ACCEL_CMD_STATUS_INVALID;
        return ACCEL_ERR_INVALID;
    }

    if (!accel_device_is_ready(dev)) {
        cmd->status = ACCEL_CMD_STATUS_REJECTED;
        return ACCEL_ERR_BUSY;
    }

    const uintptr_t base = device_mmio_base(dev);
    cmd->status = ACCEL_CMD_STATUS_PENDING;

    mmio_fence_before_device_write();
    mmio_write64(base + ACCEL_REG_CMD_BASE, (uint64_t)(uintptr_t)cmd);
    mmio_fence_before_device_write();
    mmio_write32(base + ACCEL_REG_CONTROL, ACCEL_CONTROL_START);

    platform_accel_step();

    const uint32_t status = mmio_read32(base + ACCEL_REG_STATUS);
    mmio_fence_after_device_read();

    if (status == ACCEL_STATUS_DONE && cmd->status == ACCEL_CMD_STATUS_OK) {
        return ACCEL_OK;
    }

    return ACCEL_ERR_IO;
}
