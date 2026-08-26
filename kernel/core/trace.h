#ifndef KERNEL_CORE_TRACE_H
#define KERNEL_CORE_TRACE_H

#include "core/thread.h"

#include <stdint.h>

#ifndef CONFIG_TRACE
#define CONFIG_TRACE 1
#endif

typedef enum {
    TRACE_THREAD_CREATE = 1,
    TRACE_CONTEXT_SWITCH,
    TRACE_THREAD_EXIT,
    TRACE_THREAD_SLEEP,
    TRACE_THREAD_WAKE,
    TRACE_WAIT_BLOCK,
    TRACE_WAIT_WAKE,
    TRACE_WAIT_TIMEOUT,
    TRACE_MUTEX_LOCK,
    TRACE_MUTEX_BLOCK,
    TRACE_MUTEX_TIMEOUT,
    TRACE_MUTEX_UNLOCK,
    TRACE_IDLE
} trace_type_t;

#if CONFIG_TRACE

void trace_init(void);
void trace_emit(trace_type_t type, tid_t tid, tid_t other_tid, uint64_t arg0);
void trace_dump(void);

#else

static inline void trace_init(void)
{
}

static inline void trace_emit(
    trace_type_t type,
    tid_t tid,
    tid_t other_tid,
    uint64_t arg0
)
{
    (void)type;
    (void)tid;
    (void)other_tid;
    (void)arg0;
}

static inline void trace_dump(void)
{
}

#endif

#endif
