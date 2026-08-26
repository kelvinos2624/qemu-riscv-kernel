#ifndef KERNEL_CORE_SYNC_H
#define KERNEL_CORE_SYNC_H

#include "core/thread.h"

#define MUTEX_NO_OWNER THREAD_INVALID_TID

typedef struct mutex {
    tid_t owner;
    wait_queue_t waiters;
    const char *name;
} mutex_t;

void mutex_init(mutex_t *mutex, const char *name);
void mutex_lock(mutex_t *mutex);
int mutex_lock_timeout(mutex_t *mutex, uint64_t ticks);
int mutex_trylock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);

#endif
