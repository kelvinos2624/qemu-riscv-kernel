#include "core/kernel.h"
#include "drivers/uart.h"

#define UART_THR 0
#define UART_LSR 5
#define UART_LSR_THRE (1u << 5)

static inline uint8_t mmio_read8(uintptr_t addr)
{
    return *(volatile uint8_t *)addr;
}

static inline void mmio_write8(uintptr_t addr, uint8_t value)
{
    *(volatile uint8_t *)addr = value;
}

void uart_putc(char c)
{
    while ((mmio_read8(UART0_BASE + UART_LSR) & UART_LSR_THRE) == 0) {
    }

    mmio_write8(UART0_BASE + UART_THR, (uint8_t)c);
}

void console_putc(char c)
{
    if (c == '\n') {
        uart_putc('\r');
    }

    uart_putc(c);
}

void console_write(const char *s)
{
    while (*s != '\0') {
        console_putc(*s++);
    }
}

void console_write_hex64(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";

    console_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        console_putc(digits[(value >> shift) & 0xf]);
    }
}
