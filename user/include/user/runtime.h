#ifndef USER_RUNTIME_H
#define USER_RUNTIME_H

#include <stdint.h>

void user_exit(uint64_t code) __attribute__((noreturn));
uint64_t user_yield(void);
uint64_t user_sleep(uint64_t ticks);
int user_main(void);

#endif
