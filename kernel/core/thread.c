#include "arch/riscv64/irq.h"
#include "core/kernel.h"
#include "core/thread.h"
#include "core/trap.h"
#include "drivers/timer.h"

#define THREAD_ECALL_YIELD 1u
#define THREAD_ECALL_EXIT 2u
#define THREAD_ECALL_SLEEP 3u
#define TRAP_FRAME_STACK_SIZE 288u

typedef struct tid_queue {
    tid_t tids[THREAD_MAX - 1];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} tid_queue_t;

typedef struct sleep_queue {
    tid_t tids[THREAD_MAX - 1];
    uint16_t count;
} sleep_queue_t;

extern void trap_restore(trap_frame_t *frame) __attribute__((noreturn));

static thread_t threads[THREAD_MAX];
static uint8_t thread_stacks[THREAD_MAX][THREAD_STACK_SIZE] __attribute__((aligned(16)));
static tid_queue_t ready_queue;
static sleep_queue_t sleep_queue;
static thread_t *current_thread;
static int threads_initialized;
static int threads_started;
static volatile int reschedule_requested;
static uint16_t preempt_disable_depth;

_Static_assert(THREAD_MAX > 1, "thread table must include null and real threads");
_Static_assert(THREAD_MAX - 1 < THREAD_INVALID_TID, "thread ids must fit in tid_t");
_Static_assert(sizeof(trap_frame_t) <= TRAP_FRAME_STACK_SIZE, "trap frame stack size");

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

static void sleep_queue_init(sleep_queue_t *queue)
{
    queue->count = 0;
}

static int sleep_queue_full(const sleep_queue_t *queue)
{
    return queue->count == THREAD_MAX - 1;
}

static int sleep_queue_empty(const sleep_queue_t *queue)
{
    return queue->count == 0;
}

static tid_t sleep_queue_peek(const sleep_queue_t *queue)
{
    if (sleep_queue_empty(queue)) {
        return THREAD_INVALID_TID;
    }

    return queue->tids[0];
}

static void sleep_queue_insert(sleep_queue_t *queue, tid_t tid)
{
    if (sleep_queue_full(queue)) {
        PANIC("sleep queue full");
    }

    const uint64_t wake_tick = threads[tid].wake_tick;
    uint16_t index = 0;
    while (index < queue->count &&
        threads[queue->tids[index]].wake_tick <= wake_tick) {
        index++;
    }

    for (uint16_t i = queue->count; i > index; i--) {
        queue->tids[i] = queue->tids[i - 1u];
    }

    queue->tids[index] = tid;
    queue->count++;
}

static tid_t sleep_queue_pop_head(sleep_queue_t *queue)
{
    if (sleep_queue_empty(queue)) {
        return THREAD_INVALID_TID;
    }

    tid_t tid = queue->tids[0];
    for (uint16_t i = 1; i < queue->count; i++) {
        queue->tids[i - 1u] = queue->tids[i];
    }
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

static void sleep_enqueue(tid_t tid, uint64_t wake_tick)
{
    if (!tid_is_real(tid)) {
        PANIC("attempted to sleep non-real thread");
    }

    thread_t *thread = &threads[tid];
    if (thread->state != THREAD_SLEEPING || thread->queue != THREAD_QUEUE_NONE) {
        PANIC("sleep queue invariant violation");
    }

    thread->wake_tick = wake_tick;
    sleep_queue_insert(&sleep_queue, tid);
    thread->queue = THREAD_QUEUE_SLEEP;
}

static tid_t sleep_dequeue_expired(uint64_t now)
{
    tid_t tid = sleep_queue_peek(&sleep_queue);
    if (tid == THREAD_INVALID_TID) {
        return THREAD_INVALID_TID;
    }

    thread_t *thread = &threads[tid];
    if (thread->state != THREAD_SLEEPING || thread->queue != THREAD_QUEUE_SLEEP) {
        PANIC("sleep queue contained non-sleeping thread");
    }

    if (thread->wake_tick > now) {
        return THREAD_INVALID_TID;
    }

    tid = sleep_queue_pop_head(&sleep_queue);
    thread->queue = THREAD_QUEUE_NONE;
    return tid;
}

static void wake_sleepers(uint64_t now)
{
    for (;;) {
        tid_t tid = sleep_dequeue_expired(now);
        if (tid == THREAD_INVALID_TID) {
            return;
        }

        thread_t *thread = &threads[tid];
        thread->wake_tick = 0;
        thread->state = THREAD_READY;
        ready_enqueue(tid);
    }
}

static thread_t *pick_next_thread(void)
{
    tid_t tid = ready_dequeue();
    if (tid == THREAD_INVALID_TID) {
        return &threads[THREAD_NULL_TID];
    }

    return &threads[tid];
}

static int ready_empty(void)
{
    return tid_queue_empty(&ready_queue);
}

static int current_is_preemptible(void)
{
    return threads_started &&
        current_thread != NULL &&
        current_thread->tid != THREAD_NULL_TID &&
        current_thread->state == THREAD_RUNNING &&
        preempt_disable_depth == 0 &&
        !ready_empty();
}

static void preempt_disable(void)
{
    preempt_disable_depth++;
}

static void preempt_enable(void)
{
    if (preempt_disable_depth == 0) {
        PANIC("preempt enable underflow");
    }

    preempt_disable_depth--;
}

static uintptr_t align_down(uintptr_t value, uintptr_t alignment)
{
    return value & ~(alignment - 1u);
}

static void prepare_initial_trap_frame(thread_t *thread)
{
    const uintptr_t stack_top = align_down(
        (uintptr_t)&thread_stacks[thread->tid][THREAD_STACK_SIZE],
        16
    );
    trap_frame_t *frame = (trap_frame_t *)align_down(
        stack_top - TRAP_FRAME_STACK_SIZE,
        16
    );

    uint64_t *frame_words = (uint64_t *)frame;
    for (uint16_t i = 0; i < sizeof(*frame) / sizeof(frame_words[0]); i++) {
        frame_words[i] = 0;
    }

    frame->sp = stack_top;
    frame->mepc = (uint64_t)(uintptr_t)thread_trampoline;
    frame->mstatus = MSTATUS_MPP_M | MSTATUS_MPIE;

    thread->kernel_sp = frame->sp;
    thread->trap_frame = frame;
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
    threads[tid].quantum_ticks = 0;
    threads[tid].wake_tick = 0;
    threads[tid].entry = entry;
    threads[tid].arg = arg;
    threads[tid].name = name;
    prepare_initial_trap_frame(&threads[tid]);
}

void thread_init(void)
{
    irq_state_t irq_state = irq_save();

    tid_queue_init(&ready_queue);
    sleep_queue_init(&sleep_queue);

    for (tid_t i = 0; i < THREAD_MAX; i++) {
        threads[i].tid = i;
        threads[i].state = THREAD_UNUSED;
        threads[i].queue = THREAD_QUEUE_NONE;
        threads[i].kernel_sp = 0;
        threads[i].trap_frame = NULL;
        threads[i].quantum_ticks = 0;
        threads[i].wake_tick = 0;
        threads[i].entry = NULL;
        threads[i].arg = NULL;
        threads[i].name = NULL;
    }

    current_thread = NULL;
    threads_started = 0;
    threads_initialized = 1;
    reschedule_requested = 0;
    preempt_disable_depth = 0;

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
    next->quantum_ticks = 0;
    current_thread = next;
    threads_started = 1;

    console_write("thread: starting scheduler\n");

    (void)irq_state;
    trap_restore(next->trap_frame);
}

void thread_yield(void)
{
    if (!threads_started) {
        return;
    }

    register uint64_t op __asm__("a7") = THREAD_ECALL_YIELD;
    __asm__ volatile("ecall" : : "r"(op) : "memory");
}

void thread_sleep(uint64_t ticks)
{
    if (!threads_started) {
        return;
    }

    register uint64_t arg0 __asm__("a0") = ticks;
    register uint64_t op __asm__("a7") = THREAD_ECALL_SLEEP;
    __asm__ volatile("ecall" : : "r"(arg0), "r"(op) : "memory");
}

void thread_exit(void)
{
    register uint64_t op __asm__("a7") = THREAD_ECALL_EXIT;
    __asm__ volatile("ecall" : : "r"(op) : "memory");

    PANIC("thread_exit returned");
}

tid_t thread_current_tid(void)
{
    return current_thread == NULL ? THREAD_NULL_TID : current_thread->tid;
}

const thread_t *thread_current(void)
{
    return current_thread;
}

void thread_on_timer_tick(void)
{
    if (!threads_started) {
        return;
    }

    wake_sleepers(timer_ticks());

    if (current_thread == NULL ||
        current_thread->tid == THREAD_NULL_TID ||
        current_thread->state != THREAD_RUNNING) {
        return;
    }

    if (current_thread->quantum_ticks < THREAD_QUANTUM_TICKS) {
        current_thread->quantum_ticks++;
    }

    if (current_thread->quantum_ticks >= THREAD_QUANTUM_TICKS) {
        reschedule_requested = 1;
    }
}

static trap_frame_t *switch_to_next_from_trap(trap_frame_t *frame, int requeue_current)
{
    irq_state_t irq_state = irq_save();
    preempt_disable();

    thread_t *prev = current_thread;
    if (prev == NULL || prev->state != THREAD_RUNNING) {
        PANIC("trap switch without running thread");
    }

    if (requeue_current) {
        if (prev->tid == THREAD_NULL_TID) {
            PANIC("attempted to requeue null thread");
        }

        prev->trap_frame = frame;
        prev->kernel_sp = frame->sp;
        prev->state = THREAD_READY;
        ready_enqueue(prev->tid);
    } else {
        if (prev->tid == THREAD_NULL_TID) {
            PANIC("null thread cannot exit");
        }

        prev->trap_frame = NULL;
        prev->kernel_sp = 0;
        prev->quantum_ticks = 0;
        prev->state = THREAD_EXITED;
        prev->queue = THREAD_QUEUE_NONE;
    }

    thread_t *next = pick_next_thread();
    if (next->trap_frame == NULL) {
        PANIC("next thread has no trap frame");
    }

    next->state = THREAD_RUNNING;
    next->quantum_ticks = 0;
    current_thread = next;
    reschedule_requested = 0;

    trap_frame_t *next_frame = next->trap_frame;

    preempt_enable();
    irq_restore(irq_state);
    return next_frame;
}

static trap_frame_t *switch_null_to_next_from_trap(trap_frame_t *frame)
{
    irq_state_t irq_state = irq_save();
    preempt_disable();

    thread_t *prev = current_thread;
    if (prev == NULL ||
        prev->tid != THREAD_NULL_TID ||
        prev->state != THREAD_RUNNING) {
        PANIC("null switch without running null thread");
    }

    prev->trap_frame = frame;
    prev->kernel_sp = frame->sp;
    prev->state = THREAD_READY;

    thread_t *next = pick_next_thread();
    if (next->tid == THREAD_NULL_TID || next->trap_frame == NULL) {
        PANIC("null switch without ready real thread");
    }

    next->state = THREAD_RUNNING;
    next->quantum_ticks = 0;
    current_thread = next;
    reschedule_requested = 0;

    trap_frame_t *next_frame = next->trap_frame;

    preempt_enable();
    irq_restore(irq_state);
    return next_frame;
}

static trap_frame_t *sleep_current_from_trap(trap_frame_t *frame, uint64_t ticks)
{
    irq_state_t irq_state = irq_save();
    preempt_disable();

    thread_t *prev = current_thread;
    if (prev == NULL || prev->state != THREAD_RUNNING) {
        PANIC("sleep without running thread");
    }

    if (prev->tid == THREAD_NULL_TID) {
        PANIC("null thread cannot sleep");
    }

    const uint64_t wake_tick = timer_ticks() + ticks;
    prev->trap_frame = frame;
    prev->kernel_sp = frame->sp;
    prev->quantum_ticks = 0;
    prev->state = THREAD_SLEEPING;
    sleep_enqueue(prev->tid, wake_tick);

    thread_t *next = pick_next_thread();
    if (next->trap_frame == NULL) {
        PANIC("next thread has no trap frame");
    }

    next->state = THREAD_RUNNING;
    next->quantum_ticks = 0;
    current_thread = next;
    reschedule_requested = 0;

    trap_frame_t *next_frame = next->trap_frame;

    preempt_enable();
    irq_restore(irq_state);
    return next_frame;
}

trap_frame_t *thread_handle_ecall_from_trap(trap_frame_t *frame)
{
    const uint64_t op = frame->a7;
    const uint64_t arg0 = frame->a0;

    frame->mepc += 4;

    if (!threads_started) {
        return frame;
    }

    if (op == THREAD_ECALL_YIELD) {
        if (current_thread == NULL) {
            return frame;
        }

        if (ready_empty()) {
            current_thread->quantum_ticks = 0;
            return frame;
        }

        if (current_thread->tid == THREAD_NULL_TID) {
            return switch_null_to_next_from_trap(frame);
        }

        return switch_to_next_from_trap(frame, 1);
    }

    if (op == THREAD_ECALL_EXIT) {
        return switch_to_next_from_trap(frame, 0);
    }

    if (op == THREAD_ECALL_SLEEP) {
        if (current_thread == NULL) {
            return frame;
        }

        if (current_thread->tid == THREAD_NULL_TID) {
            PANIC("null thread cannot sleep");
        }

        if (arg0 == 0) {
            if (ready_empty()) {
                current_thread->quantum_ticks = 0;
                return frame;
            }

            return switch_to_next_from_trap(frame, 1);
        }

        return sleep_current_from_trap(frame, arg0);
    }

    PANIC("unknown thread ecall");
}

trap_frame_t *thread_maybe_preempt_from_trap(trap_frame_t *frame)
{
    if (!reschedule_requested) {
        return frame;
    }

    if (!current_is_preemptible()) {
        if (threads_started &&
            current_thread != NULL &&
            current_thread->tid != THREAD_NULL_TID &&
            ready_empty()) {
            current_thread->quantum_ticks = 0;
            reschedule_requested = 0;
        }

        return frame;
    }

    return switch_to_next_from_trap(frame, 1);
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
