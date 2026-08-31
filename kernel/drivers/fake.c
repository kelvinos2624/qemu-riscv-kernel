#include "drivers/fake.h"
#include "drivers/mmio.h"

static int fake_probe(device_t *dev)
{
    const uint32_t id = mmio_read32(device_mmio_base(dev) + FAKE_REG_ID);
    return id == FAKE_DEVICE_ID_VALUE ? 0 : -1;
}

static void fake_irq_handler(device_t *dev)
{
    (void)dev;
}

const driver_t fake_driver = {
    .name = "fake-mmio",
    .compatible = FAKE_DEVICE_COMPATIBLE,
    .probe = fake_probe,
    .irq_handler = fake_irq_handler,
};
