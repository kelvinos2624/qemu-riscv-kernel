#include "core/kernel.h"
#include "core/scenario.h"
#include "core/sync.h"
#include "core/thread.h"
#include "core/trace.h"
#include "memory/page_alloc.h"

#ifndef CONFIG_SCENARIO
#define CONFIG_SCENARIO SCENARIO_SCHEDULER_SYNC
#endif

static mutex_t demo_mutex;
static volatile int demo_shared_counter;

static void scenario_idle_forever(void) __attribute__((noreturn));
static void scenario_allocator(void) __attribute__((noreturn));
static void scenario_scheduler_sync(void) __attribute__((noreturn));

static int page_is_aligned(const void *page)
{
    return (((uintptr_t)page & PAGE_MASK) == 0);
}

static void scenario_idle_forever(void)
{
    for (;;) {
        __asm__ volatile("wfi");
    }
}

static void scenario_allocator(void)
{
    console_write("scenario: allocator\n");

    const size_t initial_free = page_free_count();
    if (initial_free == 0 || page_total_count() != initial_free) {
        PANIC("page allocator empty after init");
    }

    void *first = page_alloc();
    void *second = page_alloc();
    if (first == NULL || second == NULL) {
        PANIC("page allocator failed initial allocation");
    }
    if (!page_is_aligned(first) || !page_is_aligned(second)) {
        PANIC("page allocator returned unaligned page");
    }
    if (first == second) {
        PANIC("page allocator returned duplicate page");
    }
    if (page_free_count() != initial_free - 2u) {
        PANIC("page allocator count mismatch after alloc");
    }

    page_free(first);
    void *reused = page_alloc();
    if (reused != first) {
        PANIC("page allocator did not reuse freed page");
    }

    page_free(reused);
    page_free(second);
    if (page_free_count() != initial_free) {
        PANIC("page allocator count mismatch after free");
    }

    void *allocated_pages = NULL;
    size_t exhausted_count = 0;
    for (;;) {
        void *page = page_alloc();
        if (page == NULL) {
            break;
        }

        *(void **)page = allocated_pages;
        allocated_pages = page;
        exhausted_count++;
    }

    if (exhausted_count != initial_free || page_free_count() != 0) {
        PANIC("page allocator exhaustion mismatch");
    }

    while (allocated_pages != NULL) {
        void *page = allocated_pages;
        allocated_pages = *(void **)allocated_pages;
        page_free(page);
    }

    if (page_free_count() != initial_free) {
        PANIC("page allocator count mismatch after exhaustion");
    }

    console_write("page: managed_start=");
    console_write_hex64(page_managed_start());
    console_write(" managed_end=");
    console_write_hex64(page_managed_end());
    console_write(" total=");
    console_write_hex64(page_total_count());
    console_write("\n");
    console_write("milestone 11: physical page allocator\n");

    scenario_idle_forever();
}

static void demo_thread_a(void *arg)
{
    (void)arg;

    console_write("thread: mutex-a locking\n");
    mutex_lock(&demo_mutex);
    console_write("thread: mutex-a acquired\n");
    demo_shared_counter++;
    thread_sleep(20);
    console_write("thread: mutex-a unlocking\n");
    mutex_unlock(&demo_mutex);
    thread_exit();
}

static void demo_thread_b(void *arg)
{
    (void)arg;

    console_write("thread: mutex-b timed wait\n");
    if (mutex_lock_timeout(&demo_mutex, 5) != WAIT_TIMEOUT) {
        PANIC("mutex-b acquired unexpectedly");
    }
    console_write("thread: mutex-b timed out\n");
    thread_exit();
}

static void demo_thread_c(void *arg)
{
    (void)arg;

    thread_sleep(30);
    console_write("thread: mutex-c locking\n");
    mutex_lock(&demo_mutex);
    console_write("thread: mutex-c acquired\n");
    demo_shared_counter++;
    mutex_unlock(&demo_mutex);
    console_write("milestone 10: scheduler tracing\n");
    trace_dump();
    thread_exit();
}

static void scenario_scheduler_sync(void)
{
    console_write("scenario: scheduler-sync\n");

    thread_init();
    mutex_init(&demo_mutex, "demo-mutex");
    demo_shared_counter = 0;
    if (thread_create("demo-a", demo_thread_a, NULL) < 0) {
        PANIC("failed to create demo-a");
    }
    if (thread_create("demo-b", demo_thread_b, NULL) < 0) {
        PANIC("failed to create demo-b");
    }
    if (thread_create("demo-c", demo_thread_c, NULL) < 0) {
        PANIC("failed to create demo-c");
    }
    thread_start();
}

void scenario_run(void)
{
    if (CONFIG_SCENARIO == SCENARIO_ALLOCATOR) {
        scenario_allocator();
    }

    if (CONFIG_SCENARIO == SCENARIO_SCHEDULER_SYNC) {
        scenario_scheduler_sync();
    }

    PANIC("unknown kernel scenario");
}
