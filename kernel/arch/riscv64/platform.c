#include "arch/riscv64/accel_platform.h"
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
