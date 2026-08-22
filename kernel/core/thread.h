#ifndef KERNEL_CORE_THREAD_H
#define KERNEL_CORE_THREAD_H

#include <stdint.h>

#define THREAD_NULL_TID 0
#define THREAD_MAX 8
#define THREAD_STACK_SIZE 4096

typedef enum {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_EXITED
} thread_state_t;

typedef struct thread {
    int tid;
    thread_state_t state;
    uintptr_t kernel_sp;
    void (*entry)(void *arg);
    void *arg;
    const char *name;
} thread_t;

void thread_init(void);
int thread_create(const char *name, void (*entry)(void *arg), void *arg);
void thread_start(void) __attribute__((noreturn));
void thread_yield(void);
void thread_exit(void) __attribute__((noreturn));
int thread_current_tid(void);
const thread_t *thread_current(void);

#endif
