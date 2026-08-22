#ifndef KERNEL_CORE_KERNEL_H
#define KERNEL_CORE_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#define PANIC(msg) panic(__FILE__, __LINE__, (msg))

void kmain(void) __attribute__((noreturn));
void panic(const char *file, int line, const char *msg) __attribute__((noreturn));

void console_putc(char c);
void console_write(const char *s);
void console_write_hex64(uint64_t value);

#endif
