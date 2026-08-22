#ifndef KERNEL_DRIVERS_UART_H
#define KERNEL_DRIVERS_UART_H

#include <stdint.h>

#define UART0_BASE ((uintptr_t)0x10000000)

void uart_putc(char c);

#endif
