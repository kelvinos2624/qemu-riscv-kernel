#include "arch/riscv64/accel_platform.h"
#include "arch/riscv64/irq.h"
#include "core/thread.h"
#include "drivers/accel.h"
#include "drivers/accel_cmd.h"
#include "drivers/mmio.h"
#include "memory/page_alloc.h"

_Static_assert(sizeof(accel_cmd_t) == 32u, "accelerator command descriptor size");

typedef struct accel_request_state {
    accel_cmd_t *cmd;
    wait_queue_t waiters;
    int in_use;
    int completed;
    int result;
} accel_request_state_t;

static accel_request_state_t accel_request;

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
    if (id != ACCEL_DEVICE_ID_VALUE) {
        return -1;
    }

    wait_queue_init(&accel_request.waiters, "accel-request");
    accel_request.cmd = NULL;
    accel_request.in_use = 0;
    accel_request.completed = 0;
    accel_request.result = ACCEL_ERR_IO;
    return 0;
}

static void accel_irq_handler(device_t *dev)
{
    const uintptr_t base = device_mmio_base(dev);
    const uint32_t irq_status = mmio_read32(base + ACCEL_REG_IRQ_STATUS);
    mmio_fence_after_device_read();

    const uint32_t known_irq = irq_status & (ACCEL_IRQ_DONE | ACCEL_IRQ_ERROR);
    if (known_irq == 0) {
        return;
    }

    irq_state_t irq_state = irq_save();
    if (!accel_request.in_use || accel_request.cmd == NULL) {
        mmio_fence_before_device_write();
        mmio_write32(base + ACCEL_REG_IRQ_ACK, known_irq);
        platform_accel_step();
        irq_restore(irq_state);
        return;
    }

    const uint32_t status = mmio_read32(base + ACCEL_REG_STATUS);
    mmio_fence_after_device_read();
    if ((irq_status & ACCEL_IRQ_DONE) != 0 &&
        status == ACCEL_STATUS_DONE &&
        accel_request.cmd->status == ACCEL_CMD_STATUS_OK) {
        accel_request.result = ACCEL_OK;
    } else {
        accel_request.result = ACCEL_ERR_IO;
    }
    accel_request.completed = 1;

    mmio_fence_before_device_write();
    mmio_write32(base + ACCEL_REG_IRQ_ACK, known_irq);
    platform_accel_step();
    wait_queue_wake_all(&accel_request.waiters);
    irq_restore(irq_state);
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

    irq_state_t irq_state = irq_save();
    if (accel_request.in_use) {
        irq_restore(irq_state);
        return ACCEL_ERR_BUSY;
    }

    mmio_fence_before_device_write();
    mmio_write32(device_mmio_base(dev) + ACCEL_REG_IRQ_ACK, mask);
    platform_accel_step();
    irq_restore(irq_state);
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

    irq_state_t irq_state = irq_save();
    if (accel_request.in_use) {
        irq_restore(irq_state);
        return ACCEL_ERR_BUSY;
    }

    mmio_fence_before_device_write();
    mmio_write32(device_mmio_base(dev) + ACCEL_REG_CONTROL, control);
    platform_accel_step();
    irq_restore(irq_state);
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

    const uintptr_t base = device_mmio_base(dev);

    irq_state_t irq_state = irq_save();
    if (accel_request.in_use) {
        cmd->status = ACCEL_CMD_STATUS_REJECTED;
        irq_restore(irq_state);
        return ACCEL_ERR_BUSY;
    }

    if (!accel_device_is_ready(dev)) {
        cmd->status = ACCEL_CMD_STATUS_REJECTED;
        irq_restore(irq_state);
        return ACCEL_ERR_BUSY;
    }

    accel_request.cmd = cmd;
    accel_request.in_use = 1;
    accel_request.completed = 0;
    accel_request.result = ACCEL_ERR_IO;
    cmd->status = ACCEL_CMD_STATUS_PENDING;

    mmio_fence_before_device_write();
    mmio_write64(base + ACCEL_REG_CMD_BASE, (uint64_t)(uintptr_t)cmd);
    mmio_fence_before_device_write();
    mmio_write32(base + ACCEL_REG_CONTROL, ACCEL_CONTROL_START);

    while (!accel_request.completed) {
        wait_queue_sleep(&accel_request.waiters);
    }

    const int result = accel_request.result;
    accel_request.cmd = NULL;
    accel_request.in_use = 0;
    accel_request.completed = 0;
    accel_request.result = ACCEL_ERR_IO;

    irq_restore(irq_state);
    return result;
}
