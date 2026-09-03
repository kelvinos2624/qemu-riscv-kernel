#include "core/kernel.h"
#include "drivers/accel.h"
#include "drivers/device.h"
#include "drivers/platform.h"

#define DEVICE_MAX 16u

static device_t devices[DEVICE_MAX];
static size_t device_count;
static int devices_initialized;

static const driver_t *const built_in_drivers[] = {
    &accel_driver,
};

static int string_equal(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }

    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == *b;
}

static size_t built_in_driver_count(void)
{
    return sizeof(built_in_drivers) / sizeof(built_in_drivers[0]);
}

static void device_validate_unique_irqs(void)
{
    for (size_t i = 0; i < device_count; i++) {
        const irq_t irq = devices[i].resource->irq;
        if (irq == IRQ_NONE) {
            continue;
        }

        for (size_t j = i + 1u; j < device_count; j++) {
            if (devices[j].resource->irq == irq) {
                PANIC("duplicate platform irq");
            }
        }
    }
}

void device_init(void)
{
    if (devices_initialized) {
        return;
    }

    const size_t resource_count = platform_device_resource_count();
    if (resource_count > DEVICE_MAX) {
        PANIC("too many platform devices");
    }

    for (size_t i = 0; i < resource_count; i++) {
        devices[i].resource = platform_device_resource_at(i);
        if (devices[i].resource == NULL) {
            PANIC("missing platform device resource");
        }
        devices[i].driver = NULL;
    }

    device_count = resource_count;
    device_validate_unique_irqs();
    devices_initialized = 1;
}

driver_probe_result_t driver_probe_all(void)
{
    device_init();

    driver_probe_result_t result = {
        .device_count = device_count,
        .driver_count = built_in_driver_count(),
        .bound_count = 0,
        .failed_count = 0,
    };

    for (size_t driver_index = 0; driver_index < built_in_driver_count(); driver_index++) {
        const driver_t *driver = built_in_drivers[driver_index];
        if (driver == NULL || driver->compatible == NULL || driver->probe == NULL) {
            result.failed_count++;
            continue;
        }

        for (size_t device_index = 0; device_index < device_count; device_index++) {
            device_t *dev = &devices[device_index];
            if (dev->driver != NULL ||
                !string_equal(dev->resource->compatible, driver->compatible)) {
                continue;
            }

            if (driver->probe(dev) == 0) {
                dev->driver = driver;
                result.bound_count++;
            } else {
                result.failed_count++;
            }
        }
    }

    return result;
}

device_t *device_find_by_name(const char *name)
{
    device_init();

    for (size_t i = 0; i < device_count; i++) {
        if (string_equal(devices[i].resource->name, name)) {
            return &devices[i];
        }
    }

    return NULL;
}

device_t *device_find_by_compatible(const char *compatible)
{
    device_init();

    for (size_t i = 0; i < device_count; i++) {
        if (string_equal(devices[i].resource->compatible, compatible)) {
            return &devices[i];
        }
    }

    return NULL;
}

int device_is_bound(const device_t *dev)
{
    return dev != NULL && dev->driver != NULL;
}

const char *device_name(const device_t *dev)
{
    return dev == NULL ? NULL : dev->resource->name;
}

const char *device_compatible(const device_t *dev)
{
    return dev == NULL ? NULL : dev->resource->compatible;
}

uintptr_t device_mmio_base(const device_t *dev)
{
    return dev == NULL ? 0 : dev->resource->mmio_base;
}

size_t device_mmio_size(const device_t *dev)
{
    return dev == NULL ? 0 : dev->resource->mmio_size;
}

irq_t device_irq(const device_t *dev)
{
    return dev == NULL ? IRQ_NONE : dev->resource->irq;
}

const driver_t *device_driver(const device_t *dev)
{
    return dev == NULL ? NULL : dev->driver;
}

int device_dispatch_irq(irq_t irq)
{
    if (irq == IRQ_NONE) {
        return 0;
    }

    device_init();

    for (size_t i = 0; i < device_count; i++) {
        device_t *dev = &devices[i];
        if (dev->resource->irq != irq ||
            dev->driver == NULL ||
            dev->driver->irq_handler == NULL) {
            continue;
        }

        dev->driver->irq_handler(dev);
        return 1;
    }

    return 0;
}
