#include "drivers/fake.h"
#include "drivers/platform.h"

typedef struct fake_mmio_region {
    uint32_t id;
    uint32_t scratch;
} fake_mmio_region_t;

static fake_mmio_region_t fake_mmio = {
    .id = FAKE_DEVICE_ID_VALUE,
    .scratch = 0,
};

static const device_resource_t platform_resources[] = {
    {
        .name = FAKE_DEVICE_NAME,
        .compatible = FAKE_DEVICE_COMPATIBLE,
        .mmio_base = (uintptr_t)&fake_mmio,
        .mmio_size = FAKE_MMIO_SIZE,
        .irq = FAKE_DEVICE_IRQ,
    },
};

const device_resource_t *platform_device_resources(void)
{
    return platform_resources;
}

size_t platform_device_resource_count(void)
{
    return sizeof(platform_resources) / sizeof(platform_resources[0]);
}
