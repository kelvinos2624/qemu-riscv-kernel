#include "arch/riscv64/irq.h"
#include "core/kernel.h"
#include "core/thread.h"

#include <stddef.h>

typedef struct switch_frame {
    uint64_t ra;
    uint64_t s0;
    uint64_t s1;
    uint64_t s2;
    uint64_t s3;
    uint64_t s4;
    uint64_t s5;
    uint64_t s6;
    uint64_t s7;
    uint64_t s8;
    uint64_t s9;
    uint64_t s10;
    uint64_t s11;
    uint64_t padding;
} switch_frame_t;

typedef struct tid_queue {
    tid_t tids[THREAD_MAX - 1];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} tid_queue_t;

extern void context_switch(uintptr_t *old_sp, uintptr_t new_sp);

static thread_t threads[THREAD_MAX];
static uint8_t thread_stacks[THREAD_MAX][THREAD_STACK_SIZE] __attribute__((aligned(16)));
static tid_queue_t ready_queue;
static thread_t *current_thread;
static uintptr_t boot_sp;
static int threads_initialized;
static int threads_started;

_Static_assert(offsetof(switch_frame_t, ra) == 0, "switch frame ra offset");
_Static_assert(offsetof(switch_frame_t, s0) == 8, "switch frame s0 offset");
_Static_assert(offsetof(switch_frame_t, s11) == 96, "switch frame s11 offset");
_Static_assert(sizeof(switch_frame_t) == 112, "switch frame size");
_Static_assert(THREAD_MAX > 1, "thread table must include null and real threads");
_Static_assert(THREAD_MAX - 1 < THREAD_INVALID_TID, "thread ids must fit in tid_t");

static void null_task(void *arg);
static void thread_trampoline(void) __attribute__((noreturn));

static int tid_is_valid(tid_t tid)
{
    return tid < THREAD_MAX;
}

static int tid_is_real(tid_t tid)
{
    return tid > THREAD_NULL_TID && tid < THREAD_MAX;
}

static void tid_queue_init(tid_queue_t *queue)
{
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

static int tid_queue_empty(const tid_queue_t *queue)
{
    return queue->count == 0;
}

static int tid_queue_full(const tid_queue_t *queue)
{
    return queue->count == THREAD_MAX - 1;
}

static int tid_queue_push(tid_queue_t *queue, tid_t tid)
{
    if (tid_queue_full(queue)) {
        return -1;
    }

    queue->tids[queue->tail] = tid;
    queue->tail = (uint16_t)((queue->tail + 1u) % (THREAD_MAX - 1));
    queue->count++;
    return 0;
}

static tid_t tid_queue_pop(tid_queue_t *queue)
{
    if (tid_queue_empty(queue)) {
        return THREAD_INVALID_TID;
    }

    tid_t tid = queue->tids[queue->head];
    queue->head = (uint16_t)((queue->head + 1u) % (THREAD_MAX - 1));
    queue->count--;
    return tid;
}

static void ready_enqueue(tid_t tid)
{
    if (!tid_is_real(tid)) {
        PANIC("attempted to enqueue non-real thread");
    }

    thread_t *thread = &threads[tid];
    if (thread->state != THREAD_READY || thread->queue != THREAD_QUEUE_NONE) {
        PANIC("ready queue invariant violation");
    }

    if (tid_queue_push(&ready_queue, tid) < 0) {
        PANIC("ready queue full");
    }

    thread->queue = THREAD_QUEUE_READY;
}

static tid_t ready_dequeue(void)
{
    tid_t tid = tid_queue_pop(&ready_queue);
    if (tid == THREAD_INVALID_TID) {
        return THREAD_INVALID_TID;
    }

    if (!tid_is_real(tid)) {
        PANIC("ready queue contained invalid tid");
    }

    thread_t *thread = &threads[tid];
    if (thread->state != THREAD_READY || thread->queue != THREAD_QUEUE_READY) {
        PANIC("ready queue contained non-ready thread");
    }

    thread->queue = THREAD_QUEUE_NONE;
    return tid;
}

static thread_t *pick_next_thread(void)
{
    tid_t tid = ready_dequeue();
    if (tid == THREAD_INVALID_TID) {
        return &threads[THREAD_NULL_TID];
    }

    return &threads[tid];
}

static uintptr_t align_down(uintptr_t value, uintptr_t alignment)
{
    return value & ~(alignment - 1u);
}

static void prepare_initial_stack(thread_t *thread)
{
    const uintptr_t stack_top = align_down(
        (uintptr_t)&thread_stacks[thread->tid][THREAD_STACK_SIZE],
        16
    );
    switch_frame_t *frame = (switch_frame_t *)(stack_top - sizeof(*frame));

    frame->ra = (uint64_t)(uintptr_t)thread_trampoline;
    frame->s0 = 0;
    frame->s1 = 0;
    frame->s2 = 0;
    frame->s3 = 0;
    frame->s4 = 0;
    frame->s5 = 0;
    frame->s6 = 0;
    frame->s7 = 0;
    frame->s8 = 0;
    frame->s9 = 0;
    frame->s10 = 0;
    frame->s11 = 0;
    frame->padding = 0;

    thread->kernel_sp = (uintptr_t)frame;
}

static void install_thread(
    tid_t tid,
    const char *name,
    void (*entry)(void *arg),
    void *arg
)
{
    if (!tid_is_valid(tid)) {
        PANIC("invalid thread install tid");
    }

    threads[tid].tid = tid;
    threads[tid].state = THREAD_READY;
    threads[tid].queue = THREAD_QUEUE_NONE;
    threads[tid].entry = entry;
    threads[tid].arg = arg;
    threads[tid].name = name;
    prepare_initial_stack(&threads[tid]);
}

void thread_init(void)
{
    irq_state_t irq_state = irq_save();

    tid_queue_init(&ready_queue);

    for (tid_t i = 0; i < THREAD_MAX; i++) {
        threads[i].tid = i;
        threads[i].state = THREAD_UNUSED;
        threads[i].queue = THREAD_QUEUE_NONE;
        threads[i].kernel_sp = 0;
        threads[i].entry = NULL;
        threads[i].arg = NULL;
        threads[i].name = NULL;
    }

    current_thread = NULL;
    boot_sp = 0;
    threads_started = 0;
    threads_initialized = 1;

    install_thread(THREAD_NULL_TID, "null", null_task, NULL);

    console_write("thread: initialized static table, null tid=0\n");

    irq_restore(irq_state);
}

int thread_create(const char *name, void (*entry)(void *arg), void *arg)
{
    if (!threads_initialized || entry == NULL) {
        return -1;
    }

    irq_state_t irq_state = irq_save();

    for (tid_t tid = 1; tid < THREAD_MAX; tid++) {
        if (threads[tid].state == THREAD_UNUSED || threads[tid].state == THREAD_EXITED) {
            install_thread(tid, name, entry, arg);
            ready_enqueue(tid);
            irq_restore(irq_state);
            return tid;
        }
    }

    irq_restore(irq_state);
    return -1;
}

void thread_start(void)
{
    if (!threads_initialized || threads_started) {
        PANIC("thread_start called before init or after start");
    }

    irq_state_t irq_state = irq_save();
    thread_t *next = pick_next_thread();

    next->state = THREAD_RUNNING;
    current_thread = next;
    threads_started = 1;

    console_write("thread: starting scheduler\n");

    (void)irq_state;
    context_switch(&boot_sp, next->kernel_sp);

    PANIC("thread_start returned");
}

void thread_yield(void)
{
    irq_state_t irq_state = irq_save();
    thread_t *prev = current_thread;

    if (prev == NULL || !threads_started) {
        irq_restore(irq_state);
        return;
    }

    if (prev->state != THREAD_RUNNING) {
        PANIC("current thread is not running");
    }

    if (prev->tid != THREAD_NULL_TID) {
        prev->state = THREAD_READY;
        ready_enqueue(prev->tid);
    }

    thread_t *next = pick_next_thread();

    if (next == prev) {
        next->state = THREAD_RUNNING;
        irq_restore(irq_state);
        return;
    }

    if (prev->tid == THREAD_NULL_TID) {
        prev->state = THREAD_READY;
    }

    next->state = THREAD_RUNNING;
    current_thread = next;

    context_switch(&prev->kernel_sp, next->kernel_sp);
    irq_restore(irq_state);
}

void thread_exit(void)
{
    irq_state_t irq_state = irq_save();
    thread_t *prev = current_thread;

    if (prev == NULL || prev->tid == THREAD_NULL_TID) {
        PANIC("invalid thread_exit");
    }

    prev->state = THREAD_EXITED;
    prev->queue = THREAD_QUEUE_NONE;

    thread_t *next = pick_next_thread();
    next->state = THREAD_RUNNING;
    current_thread = next;

    (void)irq_state;
    context_switch(&prev->kernel_sp, next->kernel_sp);

    PANIC("exited thread resumed");
}

tid_t thread_current_tid(void)
{
    return current_thread == NULL ? THREAD_NULL_TID : current_thread->tid;
}

const thread_t *thread_current(void)
{
    return current_thread;
}

static void thread_trampoline(void)
{
    irq_enable();

    if (current_thread == NULL || current_thread->entry == NULL) {
        PANIC("thread trampoline without current entry");
    }

    current_thread->entry(current_thread->arg);
    thread_exit();
}

static void null_task(void *arg)
{
    (void)arg;
    irq_enable();
    console_write("thread: null idle\n");

    for (;;) {
        __asm__ volatile("wfi");
        thread_yield();
    }
}
