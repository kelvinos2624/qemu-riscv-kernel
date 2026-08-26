#include "arch/riscv64/irq.h"
#include "core/kernel.h"
#include "core/trace.h"
#include "drivers/timer.h"

#if CONFIG_TRACE

#define TRACE_CAPACITY 128

typedef struct trace_event {
    uint64_t seq;
    uint64_t tick;
    trace_type_t type;
    tid_t tid;
    tid_t other_tid;
    uint64_t arg0;
} trace_event_t;

static trace_event_t trace_events[TRACE_CAPACITY];
static uint64_t trace_next_seq;
static uint16_t trace_head;
static uint16_t trace_count;
static uint64_t trace_overwrites;

static const char *trace_type_name(trace_type_t type)
{
    switch (type) {
    case TRACE_THREAD_CREATE:
        return "thread_create";
    case TRACE_CONTEXT_SWITCH:
        return "context_switch";
    case TRACE_THREAD_EXIT:
        return "thread_exit";
    case TRACE_THREAD_SLEEP:
        return "thread_sleep";
    case TRACE_THREAD_WAKE:
        return "thread_wake";
    case TRACE_WAIT_BLOCK:
        return "wait_block";
    case TRACE_WAIT_WAKE:
        return "wait_wake";
    case TRACE_WAIT_TIMEOUT:
        return "wait_timeout";
    case TRACE_MUTEX_LOCK:
        return "mutex_lock";
    case TRACE_MUTEX_BLOCK:
        return "mutex_block";
    case TRACE_MUTEX_TIMEOUT:
        return "mutex_timeout";
    case TRACE_MUTEX_UNLOCK:
        return "mutex_unlock";
    case TRACE_IDLE:
        return "idle";
    default:
        return "unknown";
    }
}

void trace_init(void)
{
    irq_state_t irq_state = irq_save();

    trace_next_seq = 0;
    trace_head = 0;
    trace_count = 0;
    trace_overwrites = 0;

    irq_restore(irq_state);
}

void trace_emit(trace_type_t type, tid_t tid, tid_t other_tid, uint64_t arg0)
{
    irq_state_t irq_state = irq_save();

    uint16_t index;
    if (trace_count < TRACE_CAPACITY) {
        index = (uint16_t)((trace_head + trace_count) % TRACE_CAPACITY);
        trace_count++;
    } else {
        index = trace_head;
        trace_head = (uint16_t)((trace_head + 1u) % TRACE_CAPACITY);
        trace_overwrites++;
    }

    trace_events[index].seq = trace_next_seq++;
    trace_events[index].tick = timer_ticks();
    trace_events[index].type = type;
    trace_events[index].tid = tid;
    trace_events[index].other_tid = other_tid;
    trace_events[index].arg0 = arg0;

    irq_restore(irq_state);
}

void trace_dump(void)
{
    irq_state_t irq_state = irq_save();

    console_write("trace: begin count=");
    console_write_hex64(trace_count);
    console_write(" overwrites=");
    console_write_hex64(trace_overwrites);
    console_write("\n");

    for (uint16_t i = 0; i < trace_count; i++) {
        const uint16_t index = (uint16_t)((trace_head + i) % TRACE_CAPACITY);
        const trace_event_t *event = &trace_events[index];

        console_write("trace: seq=");
        console_write_hex64(event->seq);
        console_write(" tick=");
        console_write_hex64(event->tick);
        console_write(" type=");
        console_write(trace_type_name(event->type));
        console_write(" tid=");
        console_write_hex64(event->tid);
        console_write(" other=");
        console_write_hex64(event->other_tid);
        console_write(" arg0=");
        console_write_hex64(event->arg0);
        console_write("\n");
    }

    console_write("trace: end\n");

    irq_restore(irq_state);
}

#endif
