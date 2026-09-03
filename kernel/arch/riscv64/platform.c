#include "arch/riscv64/accel_platform.h"
#include "core/kernel.h"
#include "drivers/accel.h"
#include "drivers/device.h"
#include "drivers/platform.h"

size_t platform_device_resource_count(void)
{
    return 1;
}

const device_resource_t *platform_device_resource_at(size_t index)
{
    if (index != 0) {
        return NULL;
    }

    return platform_accel_resource();
}

void platform_dispatch_pending_irqs(void)
{
    if (platform_accel_irq_pending() &&
        !device_dispatch_irq(ACCEL_DEVICE_IRQ)) {
        PANIC("unhandled simulated platform irq");
    }
}
