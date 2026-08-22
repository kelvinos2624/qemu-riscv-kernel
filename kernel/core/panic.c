#include "core/kernel.h"

static void console_write_u64_dec(uint64_t value)
{
    char buf[20];
    size_t i = 0;

    if (value == 0) {
        console_putc('0');
        return;
    }

    while (value != 0 && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i != 0) {
        console_putc(buf[--i]);
    }
}

void panic(const char *file, int line, const char *msg)
{
    console_write("\nPANIC at ");
    console_write(file);
    console_putc(':');
    console_write_u64_dec((uint64_t)line);
    console_write(": ");
    console_write(msg);
    console_write("\n");

    for (;;) {
        __asm__ volatile("wfi");
    }
}
