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

extern void context_switch(uintptr_t *old_sp, uintptr_t new_sp);

static thread_t threads[THREAD_MAX];
static uint8_t thread_stacks[THREAD_MAX][THREAD_STACK_SIZE] __attribute__((aligned(16)));
static thread_t *current_thread;
static uintptr_t boot_sp;
static int threads_initialized;
static int threads_started;

_Static_assert(offsetof(switch_frame_t, ra) == 0, "switch frame ra offset");
_Static_assert(offsetof(switch_frame_t, s0) == 8, "switch frame s0 offset");
_Static_assert(offsetof(switch_frame_t, s11) == 96, "switch frame s11 offset");
_Static_assert(sizeof(switch_frame_t) == 112, "switch frame size");

static void null_task(void *arg);
static void thread_trampoline(void) __attribute__((noreturn));

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

static thread_t *pick_next_thread(void)
{
    const int start_tid = current_thread == NULL ? 1 : current_thread->tid + 1;

    /* Cooperative baseline: circular TID scan, not FIFO ready-queue order. */
    for (int offset = 0; offset < THREAD_MAX - 1; offset++) {
        int tid = start_tid + offset;

        if (tid >= THREAD_MAX) {
            tid = 1 + (tid - THREAD_MAX);
        }

        if (threads[tid].state == THREAD_READY) {
            return &threads[tid];
        }
    }

    if (current_thread != NULL && current_thread->state == THREAD_RUNNING &&
        current_thread->tid != THREAD_NULL_TID) {
        return current_thread;
    }

    return &threads[THREAD_NULL_TID];
}

static void install_thread(
    int tid,
    const char *name,
    void (*entry)(void *arg),
    void *arg
)
{
    threads[tid].tid = tid;
    threads[tid].state = THREAD_READY;
    threads[tid].entry = entry;
    threads[tid].arg = arg;
    threads[tid].name = name;
    prepare_initial_stack(&threads[tid]);
}

void thread_init(void)
{
    irq_state_t irq_state = irq_save();

    for (int i = 0; i < THREAD_MAX; i++) {
        threads[i].tid = i;
        threads[i].state = THREAD_UNUSED;
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

    for (int tid = 1; tid < THREAD_MAX; tid++) {
        if (threads[tid].state == THREAD_UNUSED || threads[tid].state == THREAD_EXITED) {
            install_thread(tid, name, entry, arg);
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
    thread_t *next = pick_next_thread();

    if (prev == NULL || !threads_started) {
        irq_restore(irq_state);
        return;
    }

    if (next == prev) {
        irq_restore(irq_state);
        return;
    }

    if (prev->state == THREAD_RUNNING) {
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

    thread_t *next = pick_next_thread();
    next->state = THREAD_RUNNING;
    current_thread = next;

    (void)irq_state;
    context_switch(&prev->kernel_sp, next->kernel_sp);

    PANIC("exited thread resumed");
}

int thread_current_tid(void)
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
