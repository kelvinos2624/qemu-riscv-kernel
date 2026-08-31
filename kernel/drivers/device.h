#ifndef KERNEL_DRIVERS_DEVICE_H
#define KERNEL_DRIVERS_DEVICE_H

#include <stddef.h>
#include <stdint.h>

typedef int irq_t;

#define IRQ_NONE ((irq_t)-1)

typedef struct device device_t;
typedef struct driver driver_t;

typedef void (*irq_handler_t)(device_t *dev);
typedef int (*driver_probe_t)(device_t *dev);

typedef struct device_resource {
    const char *name;
    const char *compatible;
    uintptr_t mmio_base;
    size_t mmio_size;
    irq_t irq;
} device_resource_t;

struct driver {
    const char *name;
    const char *compatible;
    driver_probe_t probe;
    irq_handler_t irq_handler;
};

struct device {
    const device_resource_t *resource;
    const driver_t *driver;
};

typedef struct driver_probe_result {
    size_t device_count;
    size_t driver_count;
    size_t bound_count;
    size_t failed_count;
} driver_probe_result_t;

void device_init(void);
driver_probe_result_t driver_probe_all(void);
device_t *device_find_by_name(const char *name);
device_t *device_find_by_compatible(const char *compatible);
int device_is_bound(const device_t *dev);
const char *device_name(const device_t *dev);
const char *device_compatible(const device_t *dev);
uintptr_t device_mmio_base(const device_t *dev);
size_t device_mmio_size(const device_t *dev);
irq_t device_irq(const device_t *dev);
const driver_t *device_driver(const device_t *dev);

#endif
