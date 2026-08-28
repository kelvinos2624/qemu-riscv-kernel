#include "core/kernel.h"
#include "core/scenario.h"
#include "core/sync.h"
#include "core/thread.h"
#include "core/trace.h"
#include "memory/heap.h"
#include "memory/page_alloc.h"
#include "memory/vm.h"

#ifndef CONFIG_SCENARIO
#define CONFIG_SCENARIO SCENARIO_SCHEDULER_SYNC
#endif

static mutex_t demo_mutex;
static volatile int demo_shared_counter;

static void scenario_idle_forever(void) __attribute__((noreturn));
static void scenario_allocator(void) __attribute__((noreturn));
static void scenario_heap(void) __attribute__((noreturn));
static void scenario_vm(void) __attribute__((noreturn));
static void scenario_page_fault(void) __attribute__((noreturn));
static void scenario_scheduler_sync(void) __attribute__((noreturn));

static int page_is_aligned(const void *page)
{
    return (((uintptr_t)page & PAGE_MASK) == 0);
}

static int pointer_is_aligned(const void *ptr, uintptr_t alignment)
{
    return (((uintptr_t)ptr & (alignment - 1u)) == 0);
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
    if (initial_free == 0 || page_total_count() == 0) {
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

static void scenario_heap(void)
{
    console_write("scenario: heap\n");

    if (heap_page_count() != 0 ||
        heap_free_bytes() != 0 ||
        heap_allocated_bytes() != 0) {
        PANIC("heap not lazy after init");
    }

    void *small = kmalloc(1);
    if (small == NULL || !pointer_is_aligned(small, 16)) {
        PANIC("heap small allocation failed");
    }
    if (heap_page_count() != 1 || heap_allocated_bytes() != 32) {
        PANIC("heap small allocation stats mismatch");
    }

    kfree(small);
    if (heap_allocated_bytes() != 0) {
        PANIC("heap free stats mismatch");
    }

    void *reused = kmalloc(1);
    if (reused != small) {
        PANIC("heap did not reuse freed block");
    }
    kfree(reused);

    uint8_t *dirty = kmalloc(33);
    if (dirty == NULL || !pointer_is_aligned(dirty, 16)) {
        PANIC("heap 64-byte class allocation failed");
    }
    for (size_t i = 0; i < 64; i++) {
        dirty[i] = 0xa5u;
    }
    kfree(dirty);

    uint8_t *zeroed = kzalloc(33);
    if (zeroed != dirty) {
        PANIC("heap did not reuse dirty 64-byte block");
    }
    for (size_t i = 0; i < 64; i++) {
        if (zeroed[i] != 0) {
            PANIC("kzalloc did not zero entire block");
        }
    }
    kfree(zeroed);

    const size_t class_sizes[] = {
        32u,
        64u,
        128u,
        256u,
        512u,
        1024u,
        2048u,
    };
    void *class_blocks[sizeof(class_sizes) / sizeof(class_sizes[0])];
    for (size_t i = 0; i < sizeof(class_sizes) / sizeof(class_sizes[0]); i++) {
        class_blocks[i] = kmalloc(class_sizes[i]);
        if (class_blocks[i] == NULL ||
            !pointer_is_aligned(class_blocks[i], 16)) {
            PANIC("heap class allocation failed");
        }
    }
    if (heap_page_count() != sizeof(class_sizes) / sizeof(class_sizes[0])) {
        PANIC("heap lazy class growth mismatch");
    }
    for (size_t i = 0; i < sizeof(class_sizes) / sizeof(class_sizes[0]); i++) {
        kfree(class_blocks[i]);
    }

    const size_t pages_before_growth = heap_page_count();
    void *growth_blocks[130];
    size_t growth_count = 0;
    for (; growth_count < sizeof(growth_blocks) / sizeof(growth_blocks[0]);
         growth_count++) {
        growth_blocks[growth_count] = kmalloc(1);
        if (growth_blocks[growth_count] == NULL) {
            PANIC("heap growth allocation failed");
        }
    }
    if (heap_page_count() <= pages_before_growth) {
        PANIC("heap did not grow exhausted size class");
    }
    for (size_t i = 0; i < growth_count; i++) {
        kfree(growth_blocks[i]);
    }

    if (kmalloc(2049) != NULL) {
        PANIC("heap oversized allocation succeeded");
    }

    kfree(NULL);

    console_write("heap: pages=");
    console_write_hex64(heap_page_count());
    console_write(" free=");
    console_write_hex64(heap_free_bytes());
    console_write(" allocated=");
    console_write_hex64(heap_allocated_bytes());
    console_write("\n");
    console_write("milestone 12: kernel heap\n");

    scenario_idle_forever();
}

static void scenario_vm(void)
{
    console_write("scenario: vm\n");

    const size_t initial_free = page_free_count();

    vm_space_t space;
    if (vm_space_init(&space) != VM_OK || space.root == NULL) {
        PANIC("vm space init failed");
    }
    if (!page_is_aligned(space.root)) {
        PANIC("vm root page is unaligned");
    }

    void *data_page = page_alloc();
    if (data_page == NULL || !page_is_aligned(data_page)) {
        PANIC("vm data page allocation failed");
    }

    const uintptr_t va = 0x0000000040000000ull;
    const uintptr_t pa = (uintptr_t)data_page;
    const uint64_t rw_flags = VM_PTE_V | VM_PTE_R | VM_PTE_W;

    if (vm_map_page(&space, va, pa, rw_flags) != VM_OK) {
        PANIC("vm map page failed");
    }
    if (vm_translate(&space, va) != pa) {
        PANIC("vm translate base failed");
    }
    if (vm_translate(&space, va + 0x123u) != pa + 0x123u) {
        PANIC("vm translate offset failed");
    }
    if (vm_map_page(&space, va, pa, rw_flags) != VM_ERR_EXISTS) {
        PANIC("vm duplicate map did not fail");
    }
    if (vm_unmap_page(&space, va) != VM_OK) {
        PANIC("vm unmap page failed");
    }
    if (vm_translate(&space, va) != VM_TRANSLATE_INVALID) {
        PANIC("vm unmapped translation succeeded");
    }
    if (vm_unmap_page(&space, va) != VM_ERR_NOT_MAPPED) {
        PANIC("vm duplicate unmap did not fail");
    }

    void *sparse_a = page_alloc();
    void *sparse_b = page_alloc();
    if (sparse_a == NULL || sparse_b == NULL) {
        PANIC("vm sparse data allocation failed");
    }

    const uintptr_t sparse_va_a = 0x0000000000200000ull;
    const uintptr_t sparse_va_b = 0x0000002000000000ull;
    if (vm_map_page(&space, sparse_va_a, (uintptr_t)sparse_a, rw_flags) != VM_OK ||
        vm_map_page(&space, sparse_va_b, (uintptr_t)sparse_b, rw_flags) != VM_OK) {
        PANIC("vm sparse map failed");
    }
    if (vm_translate(&space, sparse_va_a) != (uintptr_t)sparse_a ||
        vm_translate(&space, sparse_va_b) != (uintptr_t)sparse_b) {
        PANIC("vm sparse translate failed");
    }

    const size_t free_before_exhaustion = page_free_count();
    void *held_pages = NULL;
    while (page_free_count() > 1) {
        void *page = page_alloc();
        if (page == NULL) {
            PANIC("vm exhaustion setup failed");
        }

        *(void **)page = held_pages;
        held_pages = page;
    }

    const uintptr_t failing_va = 0x0000003000000000ull;
    const size_t free_before_failed_map = page_free_count();
    if (free_before_failed_map != 1) {
        PANIC("vm failed-map setup mismatch");
    }
    if (vm_map_page(&space, failing_va, pa, rw_flags) != VM_ERR_NO_MEMORY) {
        PANIC("vm failed sparse map did not report no memory");
    }
    if (page_free_count() != free_before_failed_map) {
        PANIC("vm failed sparse map leaked page-table pages");
    }
    if (vm_translate(&space, failing_va) != VM_TRANSLATE_INVALID) {
        PANIC("vm failed sparse map left a mapping");
    }

    while (held_pages != NULL) {
        void *page = held_pages;
        held_pages = *(void **)held_pages;
        page_free(page);
    }
    if (page_free_count() != free_before_exhaustion) {
        PANIC("vm exhaustion cleanup mismatch");
    }

    if (vm_map_page(&space, va + 1u, pa, rw_flags) != VM_ERR_INVALID) {
        PANIC("vm accepted unaligned va");
    }
    if (vm_map_page(&space, va, pa + 1u, rw_flags) != VM_ERR_INVALID) {
        PANIC("vm accepted unaligned pa");
    }
    if (vm_map_page(&space, va, pa, VM_PTE_V | VM_PTE_W) != VM_ERR_INVALID) {
        PANIC("vm accepted write without read");
    }
    if (vm_map_page(&space, 0x0000008000000000ull, pa, rw_flags) !=
        VM_ERR_INVALID) {
        PANIC("vm accepted non-canonical va");
    }

    if (page_free_count() >= initial_free) {
        PANIC("vm did not allocate page-table pages");
    }

    console_write("vm: root=");
    console_write_hex64((uintptr_t)space.root);
    console_write(" free_pages=");
    console_write_hex64(page_free_count());
    console_write("\n");
    console_write("milestone 13: sv39 page table primitives\n");

    scenario_idle_forever();
}

static volatile uint64_t page_fault_sink;

static void scenario_fault_load(uintptr_t address)
{
    uint64_t value;

    __asm__ volatile(
        ".option push\n"
        ".option norvc\n"
        "ld %0, 0(%1)\n"
        ".option pop\n"
        : "=r"(value)
        : "r"(address)
        : "memory");

    page_fault_sink = value;
}

static void scenario_page_fault(void)
{
    console_write("scenario: page-fault\n");

    scenario_fault_load(0x0000000040000000ull);

    PANIC("page fault scenario returned from unmapped load");
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

    if (CONFIG_SCENARIO == SCENARIO_HEAP) {
        scenario_heap();
    }

    if (CONFIG_SCENARIO == SCENARIO_VM) {
        scenario_vm();
    }

    if (CONFIG_SCENARIO == SCENARIO_PAGE_FAULT) {
        scenario_page_fault();
    }

    if (CONFIG_SCENARIO == SCENARIO_SCHEDULER_SYNC) {
        scenario_scheduler_sync();
    }

    PANIC("unknown kernel scenario");
}
