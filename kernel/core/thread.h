#ifndef KERNEL_CORE_THREAD_H
#define KERNEL_CORE_THREAD_H

#include <stdint.h>

#define THREAD_NULL_TID ((tid_t)0)
#define THREAD_INVALID_TID UINT16_MAX
#define THREAD_MAX 8
#define THREAD_STACK_SIZE 4096
#define THREAD_QUANTUM_TICKS 10
#define WAIT_OK 0
#define WAIT_TIMEOUT (-1)

typedef uint16_t tid_t;
struct trap_frame;
struct user_task;
struct vm_space;

typedef struct wait_queue {
    tid_t tids[THREAD_MAX - 1];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    const char *name;
} wait_queue_t;

typedef enum {
    THREAD_UNUSED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_EXITED
} thread_state_t;

typedef enum {
    THREAD_WAIT_NONE = 0,
    THREAD_WAIT_SLEEP,
    THREAD_WAIT_QUEUE,
    THREAD_WAIT_QUEUE_TIMEOUT
} thread_wait_reason_t;

typedef struct thread {
    tid_t tid;
    thread_state_t state;
    thread_wait_reason_t wait_reason;
    wait_queue_t *wait_queue;
    uintptr_t kernel_sp;
    struct trap_frame *trap_frame;
    struct vm_space *address_space;
    struct user_task *user_task;
    uint16_t quantum_ticks;
    uint8_t in_ready_queue;
    uint8_t in_sleep_queue;
    uint8_t in_wait_queue;
    uint64_t wake_tick;
    int wait_result;
    void (*entry)(void *arg);
    void *arg;
    const char *name;
} thread_t;

void thread_init(void);
int thread_create(const char *name, void (*entry)(void *arg), void *arg);
int thread_create_user(const char *name, struct user_task *task);
void thread_start(void) __attribute__((noreturn));
void thread_yield(void);
void thread_sleep(uint64_t ticks);
void thread_exit(void) __attribute__((noreturn));
tid_t thread_current_tid(void);
const thread_t *thread_current(void);
void wait_queue_init(wait_queue_t *queue, const char *name);
void wait_queue_sleep(wait_queue_t *queue);
int wait_queue_sleep_timeout(wait_queue_t *queue, uint64_t ticks);
tid_t wait_queue_wake_one(wait_queue_t *queue);
void wait_queue_wake_all(wait_queue_t *queue);
int thread_usercopy_probe_begin(
    uintptr_t pc_start,
    uintptr_t pc_end,
    uintptr_t fixup_pc,
    uintptr_t user_start,
    uintptr_t user_end,
    uint64_t fault_cause
);
void thread_usercopy_probe_end(void);
int thread_usercopy_probe_recover(struct trap_frame *frame, uint64_t cause);
void thread_on_timer_tick(void);
struct trap_frame *thread_handle_control_trap_from_trap(struct trap_frame *frame);
struct trap_frame *thread_maybe_preempt_from_trap(struct trap_frame *frame);
struct trap_frame *thread_exit_current_from_trap(struct trap_frame *frame);
struct trap_frame *thread_yield_current_from_trap(struct trap_frame *frame);
struct trap_frame *thread_sleep_current_from_trap(struct trap_frame *frame, uint64_t ticks);
struct user_task *thread_current_user_task_for_frame(const struct trap_frame *frame);

#endif
