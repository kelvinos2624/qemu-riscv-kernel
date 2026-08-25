#include "arch/riscv64/irq.h"
#include "core/kernel.h"
#include "core/sync.h"

static tid_t require_thread_context(void)
{
    tid_t tid = thread_current_tid();
    if (tid == THREAD_NULL_TID) {
        PANIC("sync operation outside real thread context");
    }

    return tid;
}

void mutex_init(mutex_t *mutex, const char *name)
{
    if (mutex == NULL) {
        PANIC("mutex_init null mutex");
    }

    irq_state_t irq_state = irq_save();
    mutex->owner = MUTEX_NO_OWNER;
    mutex->name = name;
    wait_queue_init(&mutex->waiters, name);
    irq_restore(irq_state);
}

void mutex_lock(mutex_t *mutex)
{
    if (mutex == NULL) {
        PANIC("mutex_lock null mutex");
    }

    irq_state_t irq_state = irq_save();
    tid_t tid = require_thread_context();

    if (mutex->owner == tid) {
        PANIC("recursive mutex lock");
    }

    while (mutex->owner != MUTEX_NO_OWNER) {
        wait_queue_sleep(&mutex->waiters);

        if (mutex->owner == tid) {
            irq_restore(irq_state);
            return;
        }
    }

    mutex->owner = tid;
    irq_restore(irq_state);
}

int mutex_trylock(mutex_t *mutex)
{
    if (mutex == NULL) {
        PANIC("mutex_trylock null mutex");
    }

    irq_state_t irq_state = irq_save();
    tid_t tid = require_thread_context();

    if (mutex->owner == tid) {
        PANIC("recursive mutex trylock");
    }

    if (mutex->owner != MUTEX_NO_OWNER) {
        irq_restore(irq_state);
        return 0;
    }

    mutex->owner = tid;
    irq_restore(irq_state);
    return 1;
}

void mutex_unlock(mutex_t *mutex)
{
    if (mutex == NULL) {
        PANIC("mutex_unlock null mutex");
    }

    irq_state_t irq_state = irq_save();
    tid_t tid = require_thread_context();

    if (mutex->owner != tid) {
        PANIC("mutex unlock by non-owner");
    }

    tid_t next_owner = wait_queue_wake_one(&mutex->waiters);
    mutex->owner = next_owner == THREAD_INVALID_TID ? MUTEX_NO_OWNER : next_owner;

    irq_restore(irq_state);
}
