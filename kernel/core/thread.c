#include "arch/riscv64/csr.h"
#include "arch/riscv64/irq.h"
#include "core/kernel.h"
#include "core/thread.h"
#include "core/trace.h"
#include "core/trap.h"
#include "drivers/timer.h"
#include "user/task.h"

#define THREAD_TRAP_YIELD 1u
#define THREAD_TRAP_EXIT 2u
#define THREAD_TRAP_SLEEP 3u
#define THREAD_TRAP_WAIT 4u
#define THREAD_TRAP_WAIT_TIMEOUT 5u

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

typedef struct usercopy_probe {
    uint8_t active;
    uintptr_t pc_start;
    uintptr_t pc_end;
    uintptr_t fixup_pc;
    uintptr_t user_start;
    uintptr_t user_end;
    uint64_t fault_cause;
} usercopy_probe_t;

static thread_t threads[THREAD_MAX];
static uint8_t thread_stacks[THREAD_MAX][THREAD_STACK_SIZE] __attribute__((aligned(16)));
static usercopy_probe_t usercopy_probes[THREAD_MAX];
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
static void preempt_disable(void);
static void preempt_enable(void);
static void usercopy_probe_clear(tid_t tid);

static int tid_is_valid(tid_t tid)
{
    return tid < THREAD_MAX;
}

static int tid_is_real(tid_t tid)
{
    return tid > THREAD_NULL_TID && tid < THREAD_MAX;
}

static int trap_cause_is_usercopy_recoverable(uint64_t cause)
{
    return cause == MCAUSE_LOAD_PAGE_FAULT ||
           cause == MCAUSE_STORE_PAGE_FAULT;
}

static void usercopy_probe_clear(tid_t tid)
{
    if (!tid_is_valid(tid)) {
        return;
    }

    usercopy_probes[tid].active = 0;
    usercopy_probes[tid].pc_start = 0;
    usercopy_probes[tid].pc_end = 0;
    usercopy_probes[tid].fixup_pc = 0;
    usercopy_probes[tid].user_start = 0;
    usercopy_probes[tid].user_end = 0;
    usercopy_probes[tid].fault_cause = 0;
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

void wait_queue_init(wait_queue_t *queue, const char *name)
{
    if (queue == NULL) {
        PANIC("wait_queue_init null queue");
    }

    irq_state_t irq_state = irq_save();
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->name = name;
    irq_restore(irq_state);
}

static int wait_queue_empty(const wait_queue_t *queue)
{
    return queue->count == 0;
}

static int wait_queue_full(const wait_queue_t *queue)
{
    return queue->count == THREAD_MAX - 1;
}

static void wait_queue_push(wait_queue_t *queue, tid_t tid)
{
    if (wait_queue_full(queue)) {
        PANIC("wait queue full");
    }

    queue->tids[queue->tail] = tid;
    queue->tail = (uint16_t)((queue->tail + 1u) % (THREAD_MAX - 1));
    queue->count++;
}

static tid_t wait_queue_pop(wait_queue_t *queue)
{
    if (wait_queue_empty(queue)) {
        return THREAD_INVALID_TID;
    }

    tid_t tid = queue->tids[queue->head];
    queue->head = (uint16_t)((queue->head + 1u) % (THREAD_MAX - 1));
    queue->count--;
    return tid;
}

static int wait_queue_remove(wait_queue_t *queue, tid_t tid)
{
    uint16_t found = UINT16_MAX;

    for (uint16_t i = 0; i < queue->count; i++) {
        uint16_t index = (uint16_t)((queue->head + i) % (THREAD_MAX - 1));
        if (queue->tids[index] == tid) {
            found = i;
            break;
        }
    }

    if (found == UINT16_MAX) {
        return -1;
    }

    uint16_t write = 0;
    for (uint16_t i = 0; i < queue->count; i++) {
        uint16_t index = (uint16_t)((queue->head + i) % (THREAD_MAX - 1));
        tid_t queued_tid = queue->tids[index];
        if (i == found) {
            continue;
        }
        queue->tids[write] = queued_tid;
        write++;
    }

    queue->head = 0;
    queue->count--;
    queue->tail = queue->count;
    return 0;
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

static int sleep_queue_remove(sleep_queue_t *queue, tid_t tid)
{
    for (uint16_t i = 0; i < queue->count; i++) {
        if (queue->tids[i] != tid) {
            continue;
        }

        for (uint16_t j = (uint16_t)(i + 1u); j < queue->count; j++) {
            queue->tids[j - 1u] = queue->tids[j];
        }
        queue->count--;
        return 0;
    }

    return -1;
}

static void ready_enqueue(tid_t tid)
{
    if (!tid_is_real(tid)) {
        PANIC("attempted to enqueue non-real thread");
    }

    thread_t *thread = &threads[tid];
    if (thread->state != THREAD_READY ||
        thread->in_ready_queue ||
        thread->in_sleep_queue ||
        thread->in_wait_queue) {
        PANIC("ready queue invariant violation");
    }

    if (tid_queue_push(&ready_queue, tid) < 0) {
        PANIC("ready queue full");
    }

    thread->in_ready_queue = 1;
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
    if (thread->state != THREAD_READY || !thread->in_ready_queue) {
        PANIC("ready queue contained non-ready thread");
    }

    thread->in_ready_queue = 0;
    return tid;
}

static void sleep_enqueue(tid_t tid, uint64_t wake_tick)
{
    if (!tid_is_real(tid)) {
        PANIC("attempted to sleep non-real thread");
    }

    thread_t *thread = &threads[tid];
    if (thread->state != THREAD_BLOCKED ||
        thread->in_ready_queue ||
        thread->in_sleep_queue ||
        (thread->wait_reason != THREAD_WAIT_SLEEP &&
            thread->wait_reason != THREAD_WAIT_QUEUE_TIMEOUT)) {
        PANIC("sleep queue invariant violation");
    }

    thread->wake_tick = wake_tick;
    sleep_queue_insert(&sleep_queue, tid);
    thread->in_sleep_queue = 1;
}

static void wait_enqueue(wait_queue_t *queue, tid_t tid)
{
    if (queue == NULL) {
        PANIC("wait enqueue null queue");
    }

    if (!tid_is_real(tid)) {
        PANIC("attempted to block non-real thread");
    }

    thread_t *thread = &threads[tid];
    if (thread->state != THREAD_BLOCKED ||
        thread->in_ready_queue ||
        thread->in_wait_queue ||
        (thread->wait_reason != THREAD_WAIT_QUEUE &&
            thread->wait_reason != THREAD_WAIT_QUEUE_TIMEOUT) ||
        thread->wait_queue != NULL) {
        PANIC("wait queue invariant violation");
    }

    wait_queue_push(queue, tid);
    thread->in_wait_queue = 1;
    thread->wait_queue = queue;
}

static tid_t wait_dequeue(wait_queue_t *queue)
{
    tid_t tid = wait_queue_pop(queue);
    if (tid == THREAD_INVALID_TID) {
        return THREAD_INVALID_TID;
    }

    if (!tid_is_real(tid)) {
        PANIC("wait queue contained invalid tid");
    }

    thread_t *thread = &threads[tid];
    if (thread->state != THREAD_BLOCKED ||
        !thread->in_wait_queue ||
        thread->wait_queue != queue) {
        PANIC("wait queue contained non-blocked thread");
    }

    thread->in_wait_queue = 0;
    thread->wait_queue = NULL;
    return tid;
}

static void wait_remove(tid_t tid)
{
    thread_t *thread = &threads[tid];
    wait_queue_t *queue = thread->wait_queue;
    if (queue == NULL || !thread->in_wait_queue) {
        PANIC("wait remove invariant violation");
    }

    if (wait_queue_remove(queue, tid) < 0) {
        PANIC("wait remove missing tid");
    }

    thread->in_wait_queue = 0;
    thread->wait_queue = NULL;
}

static void timeout_remove(tid_t tid)
{
    thread_t *thread = &threads[tid];
    if (!thread->in_sleep_queue) {
        PANIC("timeout remove invariant violation");
    }

    if (sleep_queue_remove(&sleep_queue, tid) < 0) {
        PANIC("timeout remove missing tid");
    }

    thread->in_sleep_queue = 0;
    thread->wake_tick = 0;
}

static void wake_blocked_thread(tid_t tid, int result)
{
    thread_t *thread = &threads[tid];
    if (thread->state != THREAD_BLOCKED || thread->in_ready_queue) {
        PANIC("wake blocked invariant violation");
    }

    if (thread->in_sleep_queue) {
        timeout_remove(tid);
    }

    if (thread->in_wait_queue) {
        wait_remove(tid);
    }

    trace_emit(
        result == WAIT_TIMEOUT ? TRACE_WAIT_TIMEOUT : TRACE_WAIT_WAKE,
        tid,
        THREAD_INVALID_TID,
        (uint64_t)(intptr_t)result
    );

    thread->wait_reason = THREAD_WAIT_NONE;
    thread->wake_tick = 0;
    thread->wait_result = result;
    if (thread->trap_frame != NULL) {
        thread->trap_frame->a0 = (uint64_t)(intptr_t)result;
    }
    thread->state = THREAD_READY;
    ready_enqueue(tid);
}

static tid_t sleep_dequeue_expired(uint64_t now)
{
    tid_t tid = sleep_queue_peek(&sleep_queue);
    if (tid == THREAD_INVALID_TID) {
        return THREAD_INVALID_TID;
    }

    thread_t *thread = &threads[tid];
    if (thread->state != THREAD_BLOCKED ||
        !thread->in_sleep_queue ||
        (thread->wait_reason != THREAD_WAIT_SLEEP &&
            thread->wait_reason != THREAD_WAIT_QUEUE_TIMEOUT)) {
        PANIC("sleep queue contained non-timeout thread");
    }

    if (thread->wake_tick > now) {
        return THREAD_INVALID_TID;
    }

    tid = sleep_queue_pop_head(&sleep_queue);
    thread->in_sleep_queue = 0;
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
        if (thread->wait_reason == THREAD_WAIT_QUEUE_TIMEOUT) {
            if (!thread->in_wait_queue) {
                PANIC("timed wait missing wait queue membership");
            }

            wait_remove(tid);
            trace_emit(
                TRACE_THREAD_WAKE,
                tid,
                THREAD_INVALID_TID,
                (uint64_t)(intptr_t)WAIT_TIMEOUT
            );
            wake_blocked_thread(tid, WAIT_TIMEOUT);
        } else if (thread->wait_reason == THREAD_WAIT_SLEEP) {
            thread->wake_tick = 0;
            trace_emit(
                TRACE_THREAD_WAKE,
                tid,
                THREAD_INVALID_TID,
                (uint64_t)(intptr_t)WAIT_OK
            );
            wake_blocked_thread(tid, WAIT_OK);
        } else {
            PANIC("unexpected timeout wait reason");
        }
    }
}

tid_t wait_queue_wake_one(wait_queue_t *queue)
{
    if (queue == NULL) {
        PANIC("wait_queue_wake_one null queue");
    }

    irq_state_t irq_state = irq_save();
    preempt_disable();

    tid_t tid = wait_dequeue(queue);
    if (tid != THREAD_INVALID_TID) {
        wake_blocked_thread(tid, WAIT_OK);
    }

    preempt_enable();
    irq_restore(irq_state);
    return tid;
}

void wait_queue_wake_all(wait_queue_t *queue)
{
    if (queue == NULL) {
        PANIC("wait_queue_wake_all null queue");
    }

    irq_state_t irq_state = irq_save();
    preempt_disable();

    for (;;) {
        tid_t tid = wait_dequeue(queue);
        if (tid == THREAD_INVALID_TID) {
            break;
        }

        wake_blocked_thread(tid, WAIT_OK);
    }

    preempt_enable();
    irq_restore(irq_state);
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
    frame->mstatus = SSTATUS_SPP | SSTATUS_SPIE;

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
    threads[tid].wait_reason = THREAD_WAIT_NONE;
    threads[tid].wait_queue = NULL;
    threads[tid].quantum_ticks = 0;
    threads[tid].in_ready_queue = 0;
    threads[tid].in_sleep_queue = 0;
    threads[tid].in_wait_queue = 0;
    threads[tid].wake_tick = 0;
    threads[tid].wait_result = WAIT_OK;
    threads[tid].entry = entry;
    threads[tid].arg = arg;
    threads[tid].name = name;
    threads[tid].address_space = NULL;
    threads[tid].user_task = NULL;
    usercopy_probe_clear(tid);
    prepare_initial_trap_frame(&threads[tid]);
}

static void install_user_thread(tid_t tid, const char *name, user_task_t *task)
{
    trap_frame_t *frame = user_task_trap_frame(task);
    if (!tid_is_valid(tid) || frame == NULL) {
        PANIC("invalid user thread install");
    }

    const uintptr_t stack_top = align_down(
        (uintptr_t)&thread_stacks[tid][THREAD_STACK_SIZE],
        16
    );

    threads[tid].tid = tid;
    threads[tid].state = THREAD_READY;
    threads[tid].wait_reason = THREAD_WAIT_NONE;
    threads[tid].wait_queue = NULL;
    threads[tid].kernel_sp = stack_top;
    threads[tid].trap_frame = frame;
    threads[tid].address_space = &task->address_space;
    threads[tid].user_task = task;
    threads[tid].quantum_ticks = 0;
    threads[tid].in_ready_queue = 0;
    threads[tid].in_sleep_queue = 0;
    threads[tid].in_wait_queue = 0;
    threads[tid].wake_tick = 0;
    threads[tid].wait_result = WAIT_OK;
    threads[tid].entry = NULL;
    threads[tid].arg = NULL;
    threads[tid].name = name;
    task->trap_context->kernel_sp = stack_top;
    usercopy_probe_clear(tid);
}

void thread_init(void)
{
    irq_state_t irq_state = irq_save();

    trace_init();
    tid_queue_init(&ready_queue);
    sleep_queue_init(&sleep_queue);

    for (tid_t i = 0; i < THREAD_MAX; i++) {
        threads[i].tid = i;
        threads[i].state = THREAD_UNUSED;
        threads[i].wait_reason = THREAD_WAIT_NONE;
        threads[i].wait_queue = NULL;
        threads[i].kernel_sp = 0;
        threads[i].trap_frame = NULL;
        threads[i].address_space = NULL;
        threads[i].user_task = NULL;
        threads[i].quantum_ticks = 0;
        threads[i].in_ready_queue = 0;
        threads[i].in_sleep_queue = 0;
        threads[i].in_wait_queue = 0;
        threads[i].wake_tick = 0;
        threads[i].wait_result = WAIT_OK;
        threads[i].entry = NULL;
        threads[i].arg = NULL;
        threads[i].name = NULL;
        usercopy_probe_clear(i);
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
            trace_emit(TRACE_THREAD_CREATE, tid, THREAD_INVALID_TID, 0);
            irq_restore(irq_state);
            return tid;
        }
    }

    irq_restore(irq_state);
    return -1;
}

int thread_create_user(const char *name, user_task_t *task)
{
    if (!threads_initialized || user_task_trap_frame(task) == NULL) {
        return -1;
    }

    irq_state_t irq_state = irq_save();

    for (tid_t tid = 1; tid < THREAD_MAX; tid++) {
        if (threads[tid].state == THREAD_UNUSED || threads[tid].state == THREAD_EXITED) {
            install_user_thread(tid, name, task);
            ready_enqueue(tid);
            trace_emit(TRACE_THREAD_CREATE, tid, THREAD_INVALID_TID, 0);
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
    trace_emit(TRACE_CONTEXT_SWITCH, THREAD_INVALID_TID, next->tid, 0);

    console_write("thread: starting scheduler\n");

    (void)irq_state;
    trap_return_from_handler(next->trap_frame);
}

void thread_yield(void)
{
    if (!threads_started) {
        return;
    }

    register uint64_t op __asm__("a7") = THREAD_TRAP_YIELD;
    __asm__ volatile(".4byte 0x00100073" : : "r"(op) : "memory");
}

void thread_sleep(uint64_t ticks)
{
    if (!threads_started) {
        return;
    }

    register uint64_t arg0 __asm__("a0") = ticks;
    register uint64_t op __asm__("a7") = THREAD_TRAP_SLEEP;
    __asm__ volatile(".4byte 0x00100073" : : "r"(arg0), "r"(op) : "memory");
}

void wait_queue_sleep(wait_queue_t *queue)
{
    if (!threads_started) {
        return;
    }

    if (queue == NULL) {
        PANIC("wait_queue_sleep null queue");
    }

    register uintptr_t arg0 __asm__("a0") = (uintptr_t)queue;
    register uint64_t op __asm__("a7") = THREAD_TRAP_WAIT;
    __asm__ volatile(".4byte 0x00100073" : : "r"(arg0), "r"(op) : "memory");
}

int wait_queue_sleep_timeout(wait_queue_t *queue, uint64_t ticks)
{
    if (!threads_started) {
        return WAIT_TIMEOUT;
    }

    if (queue == NULL) {
        PANIC("wait_queue_sleep_timeout null queue");
    }

    if (ticks == 0) {
        return WAIT_TIMEOUT;
    }

    register uintptr_t arg0 __asm__("a0") = (uintptr_t)queue;
    register uint64_t arg1 __asm__("a1") = ticks;
    register uint64_t op __asm__("a7") = THREAD_TRAP_WAIT_TIMEOUT;
    __asm__ volatile(
        ".4byte 0x00100073"
        : "+r"(arg0)
        : "r"(arg1), "r"(op)
        : "memory"
    );
    return (int)(intptr_t)arg0;
}

void thread_exit(void)
{
    register uint64_t op __asm__("a7") = THREAD_TRAP_EXIT;
    __asm__ volatile(".4byte 0x00100073" : : "r"(op) : "memory");

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

int thread_usercopy_probe_begin(
    uintptr_t pc_start,
    uintptr_t pc_end,
    uintptr_t fixup_pc,
    uintptr_t user_start,
    uintptr_t user_end,
    uint64_t fault_cause
)
{
    irq_state_t irq_state = irq_save();

    if (current_thread == NULL ||
        current_thread->tid == THREAD_NULL_TID ||
        pc_start >= pc_end ||
        fixup_pc == 0 ||
        user_start >= user_end ||
        !trap_cause_is_usercopy_recoverable(fault_cause)) {
        irq_restore(irq_state);
        return -1;
    }

    usercopy_probe_t *probe = &usercopy_probes[current_thread->tid];
    if (probe->active) {
        irq_restore(irq_state);
        return -1;
    }

    probe->active = 1;
    probe->pc_start = pc_start;
    probe->pc_end = pc_end;
    probe->fixup_pc = fixup_pc;
    probe->user_start = user_start;
    probe->user_end = user_end;
    probe->fault_cause = fault_cause;

    irq_restore(irq_state);
    return 0;
}

void thread_usercopy_probe_end(void)
{
    irq_state_t irq_state = irq_save();

    if (current_thread != NULL && current_thread->tid != THREAD_NULL_TID) {
        usercopy_probe_clear(current_thread->tid);
    }

    irq_restore(irq_state);
}

int thread_usercopy_probe_recover(struct trap_frame *frame, uint64_t cause)
{
    if (frame == NULL ||
        current_thread == NULL ||
        current_thread->tid == THREAD_NULL_TID ||
        !trap_cause_is_usercopy_recoverable(cause)) {
        return 0;
    }

    usercopy_probe_t *probe = &usercopy_probes[current_thread->tid];
    if (!probe->active ||
        cause != probe->fault_cause ||
        frame->mepc < probe->pc_start ||
        frame->mepc >= probe->pc_end ||
        frame->mtval < probe->user_start ||
        frame->mtval >= probe->user_end) {
        return 0;
    }

    frame->mepc = probe->fixup_pc;
    probe->active = 0;
    return 1;
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
        prev->wait_reason = THREAD_WAIT_NONE;
        prev->wait_queue = NULL;
        prev->in_ready_queue = 0;
        prev->in_sleep_queue = 0;
        prev->in_wait_queue = 0;
        prev->wake_tick = 0;
        prev->wait_result = WAIT_OK;
        prev->address_space = NULL;
        prev->user_task = NULL;
        prev->state = THREAD_EXITED;
        usercopy_probe_clear(prev->tid);
    }

    thread_t *next = pick_next_thread();
    if (next->trap_frame == NULL) {
        PANIC("next thread has no trap frame");
    }

    next->state = THREAD_RUNNING;
    next->quantum_ticks = 0;
    current_thread = next;
    reschedule_requested = 0;
    trace_emit(
        requeue_current ? TRACE_CONTEXT_SWITCH : TRACE_THREAD_EXIT,
        prev->tid,
        next->tid,
        requeue_current ? 0 : 1
    );
    if (!requeue_current) {
        trace_emit(TRACE_CONTEXT_SWITCH, prev->tid, next->tid, 0);
    }

    trap_frame_t *next_frame = next->trap_frame;

    preempt_enable();
    irq_restore(irq_state);
    return next_frame;
}

trap_frame_t *thread_exit_current_from_trap(trap_frame_t *frame)
{
    return switch_to_next_from_trap(frame, 0);
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
    trace_emit(TRACE_CONTEXT_SWITCH, prev->tid, next->tid, 0);

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
    prev->wait_reason = THREAD_WAIT_SLEEP;
    prev->wait_result = WAIT_OK;
    prev->state = THREAD_BLOCKED;
    sleep_enqueue(prev->tid, wake_tick);
    trace_emit(TRACE_THREAD_SLEEP, prev->tid, THREAD_INVALID_TID, wake_tick);

    thread_t *next = pick_next_thread();
    if (next->trap_frame == NULL) {
        PANIC("next thread has no trap frame");
    }

    next->state = THREAD_RUNNING;
    next->quantum_ticks = 0;
    current_thread = next;
    reschedule_requested = 0;
    trace_emit(TRACE_CONTEXT_SWITCH, prev->tid, next->tid, 0);

    trap_frame_t *next_frame = next->trap_frame;

    preempt_enable();
    irq_restore(irq_state);
    return next_frame;
}

static trap_frame_t *block_current_from_trap(
    trap_frame_t *frame,
    wait_queue_t *queue,
    uint64_t timeout_ticks
)
{
    irq_state_t irq_state = irq_save();
    preempt_disable();

    thread_t *prev = current_thread;
    if (prev == NULL || prev->state != THREAD_RUNNING) {
        PANIC("wait without running thread");
    }

    if (prev->tid == THREAD_NULL_TID) {
        PANIC("null thread cannot wait");
    }

    prev->trap_frame = frame;
    prev->kernel_sp = frame->sp;
    prev->quantum_ticks = 0;
    prev->wait_reason = timeout_ticks == 0 ?
        THREAD_WAIT_QUEUE :
        THREAD_WAIT_QUEUE_TIMEOUT;
    prev->wait_result = WAIT_OK;
    prev->state = THREAD_BLOCKED;
    wait_enqueue(queue, prev->tid);
    if (timeout_ticks != 0) {
        sleep_enqueue(prev->tid, timer_ticks() + timeout_ticks);
    }
    trace_emit(TRACE_WAIT_BLOCK, prev->tid, THREAD_INVALID_TID, timeout_ticks);

    thread_t *next = pick_next_thread();
    if (next->trap_frame == NULL) {
        PANIC("next thread has no trap frame");
    }

    next->state = THREAD_RUNNING;
    next->quantum_ticks = 0;
    current_thread = next;
    reschedule_requested = 0;
    trace_emit(TRACE_CONTEXT_SWITCH, prev->tid, next->tid, 0);

    trap_frame_t *next_frame = next->trap_frame;

    preempt_enable();
    irq_restore(irq_state);
    return next_frame;
}

trap_frame_t *thread_handle_control_trap_from_trap(trap_frame_t *frame)
{
    const uint64_t op = frame->a7;
    const uint64_t arg0 = frame->a0;

    frame->mepc += 4;

    if (!threads_started) {
        return frame;
    }

    if (op == THREAD_TRAP_YIELD) {
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

    if (op == THREAD_TRAP_EXIT) {
        return switch_to_next_from_trap(frame, 0);
    }

    if (op == THREAD_TRAP_SLEEP) {
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

    if (op == THREAD_TRAP_WAIT) {
        wait_queue_t *queue = (wait_queue_t *)(uintptr_t)arg0;
        if (queue == NULL) {
            PANIC("wait control trap null queue");
        }

        if (current_thread == NULL) {
            return frame;
        }

        if (current_thread->tid == THREAD_NULL_TID) {
            PANIC("null thread cannot wait");
        }

        return block_current_from_trap(frame, queue, 0);
    }

    if (op == THREAD_TRAP_WAIT_TIMEOUT) {
        wait_queue_t *queue = (wait_queue_t *)(uintptr_t)arg0;
        const uint64_t ticks = frame->a1;
        if (queue == NULL) {
            PANIC("timed wait control trap null queue");
        }

        if (current_thread == NULL) {
            frame->a0 = (uint64_t)(intptr_t)WAIT_TIMEOUT;
            return frame;
        }

        if (current_thread->tid == THREAD_NULL_TID) {
            PANIC("null thread cannot timed wait");
        }

        if (ticks == 0) {
            frame->a0 = (uint64_t)(intptr_t)WAIT_TIMEOUT;
            return frame;
        }

        return block_current_from_trap(frame, queue, ticks);
    }

    PANIC("unknown thread control trap");
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

user_task_t *thread_current_user_task_for_frame(const trap_frame_t *frame)
{
    if (frame == NULL ||
        current_thread == NULL ||
        current_thread->user_task == NULL ||
        current_thread->trap_frame != frame ||
        (frame->mstatus & SSTATUS_SPP) != 0) {
        return NULL;
    }

    return current_thread->user_task;
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
    trace_emit(TRACE_IDLE, THREAD_NULL_TID, THREAD_INVALID_TID, 0);
    console_write("thread: null idle\n");

    for (;;) {
        __asm__ volatile("wfi");
        thread_yield();
    }
}
