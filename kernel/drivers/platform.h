#ifndef KERNEL_DRIVERS_PLATFORM_H
#define KERNEL_DRIVERS_PLATFORM_H

#include <stddef.h>

#include "drivers/device.h"

const device_resource_t *platform_device_resources(void);
size_t platform_device_resource_count(void);

#endif
