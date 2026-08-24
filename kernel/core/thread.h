#ifndef KERNEL_CORE_THREAD_H
#define KERNEL_CORE_THREAD_H

#include <stdint.h>

#define THREAD_NULL_TID ((tid_t)0)
#define THREAD_INVALID_TID UINT16_MAX
#define THREAD_MAX 8
#define THREAD_STACK_SIZE 4096
#define THREAD_QUANTUM_TICKS 10

typedef uint16_t tid_t;
struct trap_frame;

typedef enum {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_SLEEPING,
    THREAD_EXITED
} thread_state_t;

typedef enum {
    THREAD_QUEUE_NONE = 0,
    THREAD_QUEUE_READY,
    THREAD_QUEUE_SLEEP,
    THREAD_QUEUE_WAIT
} thread_queue_t;

typedef struct thread {
    tid_t tid;
    thread_state_t state;
    thread_queue_t queue;
    uintptr_t kernel_sp;
    struct trap_frame *trap_frame;
    uint16_t quantum_ticks;
    uint64_t wake_tick;
    void (*entry)(void *arg);
    void *arg;
    const char *name;
} thread_t;

void thread_init(void);
int thread_create(const char *name, void (*entry)(void *arg), void *arg);
void thread_start(void) __attribute__((noreturn));
void thread_yield(void);
void thread_sleep(uint64_t ticks);
void thread_exit(void) __attribute__((noreturn));
tid_t thread_current_tid(void);
const thread_t *thread_current(void);
void thread_on_timer_tick(void);
struct trap_frame *thread_handle_ecall_from_trap(struct trap_frame *frame);
struct trap_frame *thread_maybe_preempt_from_trap(struct trap_frame *frame);

#endif
