#include "arch/riscv64/accel_platform.h"
#include "drivers/accel.h"
#include "drivers/accel_cmd.h"
#include "memory/page_alloc.h"

typedef struct accel_mmio_region {
    uint32_t id;
    uint32_t status;
    uint32_t control;
    uint32_t irq_status;
    uint32_t irq_ack;
    uint32_t reserved;
    uint64_t cmd_base;
} accel_mmio_region_t;

static accel_mmio_region_t accel_mmio = {
    .id = ACCEL_DEVICE_ID_VALUE,
    .status = ACCEL_STATUS_IDLE,
    .control = 0,
    .irq_status = 0,
    .irq_ack = 0,
    .reserved = 0,
    .cmd_base = 0,
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
    accel_mmio.cmd_base = 0;
}

static int accel_descriptor_location_is_valid(uintptr_t addr)
{
    return addr != 0 &&
        (addr & (ACCEL_CMD_ALIGNMENT - 1u)) == 0 &&
        page_range_is_managed_page_contained(addr, sizeof(accel_cmd_t));
}

static int accel_descriptor_payload_is_valid(const accel_cmd_t *cmd)
{
    if (cmd->op != ACCEL_CMD_OP_MEMSET ||
        cmd->flags != 0 ||
        cmd->reserved != 0 ||
        cmd->status != ACCEL_CMD_STATUS_PENDING ||
        cmd->len == 0) {
        return 0;
    }

    return page_range_is_managed_page_contained(
        (uintptr_t)cmd->dst_pa,
        (size_t)cmd->len);
}

static void accel_execute_memset(accel_cmd_t *cmd)
{
    uint8_t *dst = (uint8_t *)(uintptr_t)cmd->dst_pa;
    const uint8_t value = (uint8_t)cmd->value;

    for (uint32_t i = 0; i < cmd->len; i++) {
        dst[i] = value;
    }

    cmd->status = ACCEL_CMD_STATUS_OK;
    accel_mmio.status = ACCEL_STATUS_DONE;
    accel_mmio.irq_status = ACCEL_IRQ_DONE;
}

static void accel_start_descriptor(uintptr_t cmd_base)
{
    if (!accel_descriptor_location_is_valid(cmd_base)) {
        accel_enter_error();
        return;
    }

    accel_cmd_t *cmd = (accel_cmd_t *)cmd_base;
    if (!accel_descriptor_payload_is_valid(cmd)) {
        cmd->status = ACCEL_CMD_STATUS_INVALID;
        accel_enter_error();
        return;
    }

    accel_execute_memset(cmd);
}

static void accel_start(void)
{
    if (accel_mmio.status != ACCEL_STATUS_IDLE) {
        accel_enter_error();
        return;
    }

    accel_mmio.status = ACCEL_STATUS_BUSY;
    if (accel_mmio.cmd_base == 0) {
        accel_mmio.status = ACCEL_STATUS_DONE;
        accel_mmio.irq_status = ACCEL_IRQ_DONE;
        return;
    }

    accel_start_descriptor((uintptr_t)accel_mmio.cmd_base);
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
