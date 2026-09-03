#ifndef KERNEL_DRIVERS_PLATFORM_H
#define KERNEL_DRIVERS_PLATFORM_H

#include <stddef.h>

#include "drivers/device.h"

size_t platform_device_resource_count(void);
const device_resource_t *platform_device_resource_at(size_t index);
void platform_dispatch_pending_irqs(void);

#endif
