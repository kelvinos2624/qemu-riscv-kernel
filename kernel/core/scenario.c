#include "arch/riscv64/csr.h"
#include "core/kernel.h"
#include "core/scenario.h"
#include "core/sync.h"
#include "core/thread.h"
#include "core/trace.h"
#include "core/trap.h"
#include "drivers/accel.h"
#include "drivers/accel_cmd.h"
#include "drivers/device.h"
#include "memory/heap.h"
#include "memory/page_alloc.h"
#include "memory/paging.h"
#include "memory/user_space.h"
#include "memory/usercopy.h"
#include "memory/vm.h"

#ifndef CONFIG_SCENARIO
#define CONFIG_SCENARIO SCENARIO_SCHEDULER_SYNC
#endif

static mutex_t demo_mutex;
static volatile int demo_shared_counter;
static accel_cmd_t static_accel_cmd;

static void scenario_idle_forever(void) __attribute__((noreturn));
static void scenario_allocator(void) __attribute__((noreturn));
static void scenario_heap(void) __attribute__((noreturn));
static void scenario_vm(void) __attribute__((noreturn));
static void scenario_page_fault(void) __attribute__((noreturn));
static void scenario_user_space(void) __attribute__((noreturn));
static void scenario_first_user(void) __attribute__((noreturn));
static void scenario_usercopy(void) __attribute__((noreturn));
static void scenario_scheduler_sync(void) __attribute__((noreturn));
static void scenario_driver_framework(void) __attribute__((noreturn));
static void scenario_accel_registers(void) __attribute__((noreturn));
static void scenario_accelerator_descriptors(void) __attribute__((noreturn));

extern char first_user_start[];
extern char first_user_end[];

static int page_is_aligned(const void *page)
{
    return (((uintptr_t)page & PAGE_MASK) == 0);
}

static int pointer_is_aligned(const void *ptr, uintptr_t alignment)
{
    return (((uintptr_t)ptr & (alignment - 1u)) == 0);
}

static void memory_copy(void *dst, const void *src, size_t size)
{
    uint8_t *dst_bytes = dst;
    const uint8_t *src_bytes = src;
    for (size_t i = 0; i < size; i++) {
        dst_bytes[i] = src_bytes[i];
    }
}

static void memory_zero(void *ptr, size_t size)
{
    uint8_t *bytes = ptr;
    for (size_t i = 0; i < size; i++) {
        bytes[i] = 0;
    }
}

static void scenario_init_memset_cmd(
    accel_cmd_t *cmd,
    void *dst,
    uint32_t len,
    uint32_t value)
{
    cmd->op = ACCEL_CMD_OP_MEMSET;
    cmd->flags = 0;
    cmd->dst_pa = (uint64_t)(uintptr_t)dst;
    cmd->len = len;
    cmd->value = value;
    cmd->status = ACCEL_CMD_STATUS_ERROR;
    cmd->reserved = 0;
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

static void scenario_user_space(void)
{
    console_write("scenario: user-space\n");

    const size_t initial_free = page_free_count();

    vm_space_t space;
    if (vm_space_init(&space) != VM_OK || space.root == NULL) {
        PANIC("user vm space init failed");
    }

    void *code_page = page_alloc();
    void *stack_page = page_alloc();
    if (code_page == NULL || stack_page == NULL) {
        PANIC("user backing page allocation failed");
    }

    if (user_space_map_code_page(&space, (uintptr_t)code_page) != VM_OK) {
        PANIC("user code map failed");
    }
    if (user_space_map_stack_page(&space, (uintptr_t)stack_page) != VM_OK) {
        PANIC("user stack map failed");
    }
    if (vm_translate(&space, USER_SPACE_CODE_BASE) != (uintptr_t)code_page) {
        PANIC("user code translate failed");
    }
    if (vm_translate(&space, USER_SPACE_STACK_BASE) != (uintptr_t)stack_page) {
        PANIC("user stack translate failed");
    }

    const uint64_t user_rw = VM_PTE_V | VM_PTE_R | VM_PTE_W | VM_PTE_U;
    if (user_space_map_page(&space, 0, (uintptr_t)code_page, user_rw) !=
        VM_ERR_INVALID) {
        PANIC("user null guard accepted");
    }
    if (user_space_map_page(
            &space,
            USER_SPACE_CODE_BASE + PAGE_SIZE,
            (uintptr_t)code_page,
            VM_PTE_V | VM_PTE_R | VM_PTE_W
        ) != VM_ERR_INVALID) {
        PANIC("user map without U accepted");
    }
    if (user_space_map_page(
            &space,
            0x0000000010000000ull,
            (uintptr_t)code_page,
            user_rw
        ) != VM_ERR_INVALID) {
        PANIC("user mmio-looking va accepted");
    }
    if (user_space_map_page(
            &space,
            USER_SPACE_CODE_BASE + PAGE_SIZE,
            0x0000000010000000ull,
            user_rw
        ) != VM_ERR_INVALID) {
        PANIC("user non-managed pa accepted");
    }
    if (user_space_map_page(
            &space,
            USER_SPACE_CODE_BASE + PAGE_SIZE,
            (uintptr_t)code_page,
            user_rw | VM_PTE_G
        ) != VM_ERR_INVALID) {
        PANIC("user global mapping accepted");
    }
    if (vm_space_destroy(&space) != VM_ERR_BUSY) {
        PANIC("busy user vm space destroy accepted");
    }

    if (vm_unmap_page(&space, USER_SPACE_CODE_BASE) != VM_OK ||
        vm_unmap_page(&space, USER_SPACE_STACK_BASE) != VM_OK) {
        PANIC("user unmap failed");
    }

    page_free(code_page);
    page_free(stack_page);

    if (vm_space_destroy(&space) != VM_OK || space.root != NULL) {
        PANIC("user vm space destroy failed");
    }
    if (page_free_count() != initial_free) {
        PANIC("user vm space leaked pages");
    }

    console_write("user: code=");
    console_write_hex64(USER_SPACE_CODE_BASE);
    console_write(" stack=");
    console_write_hex64(USER_SPACE_STACK_BASE);
    console_write(" top=");
    console_write_hex64(USER_SPACE_TOP);
    console_write("\n");
    console_write("milestone 14: user address space skeleton\n");

    scenario_idle_forever();
}

static void scenario_first_user(void)
{
    console_write("scenario: first-user\n");

    const size_t user_program_size =
        (size_t)(first_user_end - first_user_start);
    if (user_program_size == 0 || user_program_size > PAGE_SIZE) {
        PANIC("invalid first user program size");
    }

    uint8_t *code_page = page_alloc();
    uint8_t *stack_page = page_alloc();
    uint8_t *trap_stack_page = page_alloc();
    if (code_page == NULL || stack_page == NULL || trap_stack_page == NULL) {
        PANIC("first user page allocation failed");
    }

    memory_zero(code_page, PAGE_SIZE);
    memory_zero(stack_page, PAGE_SIZE);
    memory_zero(trap_stack_page, PAGE_SIZE);
    memory_copy(code_page, first_user_start, user_program_size);
    __asm__ volatile("fence.i" : : : "memory");

    vm_space_t *kernel_space = paging_kernel_space();
    if (user_space_map_code_page(kernel_space, (uintptr_t)code_page) != VM_OK) {
        PANIC("first user code map failed");
    }
    if (user_space_map_stack_page(kernel_space, (uintptr_t)stack_page) != VM_OK) {
        PANIC("first user stack map failed");
    }

    trap_frame_t *frame = (trap_frame_t *)(
        (uintptr_t)trap_stack_page + PAGE_SIZE - TRAP_FRAME_STACK_SIZE
    );
    memory_zero(frame, TRAP_FRAME_STACK_SIZE);
    frame->sp = USER_SPACE_STACK_TOP;
    frame->mepc = USER_SPACE_CODE_BASE;
    frame->mstatus = SSTATUS_SPIE;

    console_write("user: entering u-mode pc=");
    console_write_hex64(frame->mepc);
    console_write(" sp=");
    console_write_hex64(frame->sp);
    console_write("\n");

    trap_restore(frame);
}

static void expect_usercopy_invalid(int result, const char *name)
{
    if (result != USERCOPY_ERR_INVALID) {
        console_write("usercopy: expected invalid for ");
        console_write(name);
        console_write("\n");
        PANIC("usercopy invalid check failed");
    }
}

static void usercopy_worker(void *arg)
{
    (void)arg;

    uint8_t *page_a = page_alloc();
    uint8_t *page_b = page_alloc();
    uint8_t *read_only_page = page_alloc();
    if (page_a == NULL || page_b == NULL || read_only_page == NULL) {
        PANIC("usercopy backing page allocation failed");
    }

    memory_zero(page_a, PAGE_SIZE);
    memory_zero(page_b, PAGE_SIZE);
    memory_zero(read_only_page, PAGE_SIZE);

    const uintptr_t user_a = USER_SPACE_CODE_BASE;
    const uintptr_t user_b = USER_SPACE_CODE_BASE + PAGE_SIZE;
    const uintptr_t user_read_only = USER_SPACE_CODE_BASE + (4u * PAGE_SIZE);
    const uint64_t user_rw =
        VM_PTE_V | VM_PTE_R | VM_PTE_W | VM_PTE_U | VM_PTE_A | VM_PTE_D;
    const uint64_t user_ro = VM_PTE_V | VM_PTE_R | VM_PTE_U | VM_PTE_A;

    if (user_space_map_page(paging_kernel_space(), user_a, (uintptr_t)page_a, user_rw) !=
            VM_OK ||
        user_space_map_page(paging_kernel_space(), user_b, (uintptr_t)page_b, user_rw) !=
            VM_OK ||
        user_space_map_page(
            paging_kernel_space(),
            user_read_only,
            (uintptr_t)read_only_page,
            user_ro
        ) != VM_OK) {
        PANIC("usercopy map failed");
    }

    for (size_t i = 0; i < PAGE_SIZE; i++) {
        page_a[i] = (uint8_t)(0x10u + (i & 0x0fu));
        page_b[i] = (uint8_t)(0x80u + (i & 0x0fu));
    }

    uint8_t kernel_buf[8];
    if (copy_from_user(kernel_buf, (const void *)(user_b - 4u), sizeof(kernel_buf)) !=
        USERCOPY_OK) {
        PANIC("usercopy cross-page read failed");
    }
    for (size_t i = 0; i < 4; i++) {
        if (kernel_buf[i] != page_a[PAGE_SIZE - 4u + i]) {
            PANIC("usercopy first-page read mismatch");
        }
        if (kernel_buf[4u + i] != page_b[i]) {
            PANIC("usercopy second-page read mismatch");
        }
    }

    const uint8_t write_buf[8] = {
        0xa0u,
        0xa1u,
        0xa2u,
        0xa3u,
        0xa4u,
        0xa5u,
        0xa6u,
        0xa7u,
    };
    if (copy_to_user((void *)(user_b - 2u), write_buf, sizeof(write_buf)) !=
        USERCOPY_OK) {
        PANIC("usercopy cross-page write failed");
    }
    if (page_a[PAGE_SIZE - 2u] != write_buf[0] ||
        page_a[PAGE_SIZE - 1u] != write_buf[1]) {
        PANIC("usercopy first-page write mismatch");
    }
    for (size_t i = 0; i < 6; i++) {
        if (page_b[i] != write_buf[2u + i]) {
            PANIC("usercopy second-page write mismatch");
        }
    }

    expect_usercopy_invalid(
        copy_from_user(kernel_buf, (const void *)0, 1),
        "null guard"
    );
    expect_usercopy_invalid(
        copy_from_user(kernel_buf, (const void *)UINTPTR_MAX, 2),
        "overflow"
    );
    expect_usercopy_invalid(
        copy_from_user(kernel_buf, (const void *)(user_b + PAGE_SIZE - 4u), 8),
        "unmapped cross-page"
    );
    expect_usercopy_invalid(
        copy_from_user(kernel_buf, (const void *)0x0000000010000000ull, 1),
        "mmio-looking va"
    );
    expect_usercopy_invalid(
        copy_to_user((void *)user_read_only, write_buf, 1),
        "write to read-only mapping"
    );

    if (usercopy_recoverable_fault_selftest() != USERCOPY_ERR_FAULT) {
        PANIC("usercopy recoverable fault selftest failed");
    }

    if (vm_unmap_page(paging_kernel_space(), user_a) != VM_OK ||
        vm_unmap_page(paging_kernel_space(), user_b) != VM_OK ||
        vm_unmap_page(paging_kernel_space(), user_read_only) != VM_OK) {
        PANIC("usercopy unmap failed");
    }

    page_free(page_a);
    page_free(page_b);
    page_free(read_only_page);

    console_write("usercopy: passed\n");
    console_write("milestone 16: safe usercopy\n");
    thread_exit();
}

static void scenario_usercopy(void)
{
    console_write("scenario: usercopy\n");

    thread_init();
    if (thread_create("usercopy", usercopy_worker, NULL) < 0) {
        PANIC("failed to create usercopy worker");
    }
    thread_start();
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

static void scenario_driver_framework(void)
{
    console_write("scenario: driver-framework\n");

    device_t *by_name = device_find_by_name(ACCEL_DEVICE_NAME);
    if (by_name == NULL) {
        PANIC("accelerator lookup by name failed");
    }

    device_t *by_compatible = device_find_by_compatible(ACCEL_DEVICE_COMPATIBLE);
    if (by_compatible != by_name) {
        PANIC("accelerator lookup by compatible failed");
    }

    if (!device_is_bound(by_name)) {
        PANIC("accelerator did not bind");
    }

    if (device_driver(by_name) == NULL ||
        device_driver(by_name)->irq_handler == NULL ||
        device_irq(by_name) != ACCEL_DEVICE_IRQ ||
        device_mmio_size(by_name) < ACCEL_MMIO_SIZE) {
        PANIC("accelerator metadata invalid");
    }

    if (accel_reset() != ACCEL_OK || accel_start_selftest() != ACCEL_OK) {
        PANIC("accelerator framework operation failed");
    }

    uint32_t status = 0;
    if (accel_get_status(&status) != ACCEL_OK || status != ACCEL_STATUS_DONE) {
        PANIC("accelerator framework status invalid");
    }

    console_write("driver: accelerator device bound\n");
    console_write("driver: accelerator mmio path passed\n");
    console_write("milestone 17: driver framework\n");

    scenario_idle_forever();
}

static void expect_accel_status(uint32_t expected, const char *name)
{
    uint32_t status = 0;
    if (accel_get_status(&status) != ACCEL_OK || status != expected) {
        console_write("accel: unexpected status for ");
        console_write(name);
        console_write(" value=");
        console_write_hex64(status);
        console_write("\n");
        PANIC("accelerator status mismatch");
    }
}

static void expect_accel_irq_status(uint32_t expected, const char *name)
{
    uint32_t irq_status = 0;
    if (accel_get_irq_status(&irq_status) != ACCEL_OK || irq_status != expected) {
        console_write("accel: unexpected irq status for ");
        console_write(name);
        console_write(" value=");
        console_write_hex64(irq_status);
        console_write("\n");
        PANIC("accelerator irq status mismatch");
    }
}

static void scenario_accel_registers(void)
{
    console_write("scenario: accel-registers\n");

    device_t *dev = device_find_by_name(ACCEL_DEVICE_NAME);
    if (dev == NULL || !device_is_bound(dev)) {
        PANIC("accelerator device unavailable");
    }

    if (accel_reset() != ACCEL_OK) {
        PANIC("accelerator reset failed");
    }
    expect_accel_status(ACCEL_STATUS_IDLE, "reset");
    expect_accel_irq_status(0, "reset");

    if (accel_start_selftest() != ACCEL_OK) {
        PANIC("accelerator start failed");
    }
    expect_accel_status(ACCEL_STATUS_DONE, "start");
    expect_accel_irq_status(ACCEL_IRQ_DONE, "start");

    if (accel_ack_irq(ACCEL_IRQ_DONE) != ACCEL_OK) {
        PANIC("accelerator done ack failed");
    }
    expect_accel_status(ACCEL_STATUS_DONE, "ack-done");
    expect_accel_irq_status(0, "ack-done");

    if (accel_start_selftest() != ACCEL_OK) {
        PANIC("accelerator invalid start command failed");
    }
    expect_accel_status(ACCEL_STATUS_ERROR, "start-after-done");
    expect_accel_irq_status(ACCEL_IRQ_ERROR, "start-after-done");

    if (accel_ack_irq(ACCEL_IRQ_ERROR) != ACCEL_OK) {
        PANIC("accelerator error ack failed");
    }
    expect_accel_status(ACCEL_STATUS_ERROR, "ack-error");
    expect_accel_irq_status(0, "ack-error");

    if (accel_write_control_raw(ACCEL_CONTROL_RESET | ACCEL_CONTROL_START) != ACCEL_OK) {
        PANIC("accelerator reset-start command failed");
    }
    expect_accel_status(ACCEL_STATUS_IDLE, "reset-start");
    expect_accel_irq_status(0, "reset-start");

    if (accel_write_control_raw(ACCEL_CONTROL_START | (1u << 31)) != ACCEL_ERR_INVALID) {
        PANIC("accelerator invalid control bits accepted");
    }
    expect_accel_status(ACCEL_STATUS_IDLE, "invalid-control");
    expect_accel_irq_status(0, "invalid-control");

    console_write("accel: reset idle\n");
    console_write("accel: start done\n");
    console_write("accel: invalid transition error\n");
    console_write("accel: reset priority passed\n");
    console_write("milestone 18: simulated accelerator registers\n");

    scenario_idle_forever();
}

static void scenario_accelerator_descriptors(void)
{
    console_write("scenario: accelerator-descriptors\n");

    void *cmd_page = page_alloc();
    void *buffer_page = page_alloc();
    if (cmd_page == NULL || buffer_page == NULL) {
        PANIC("accelerator descriptor pages unavailable");
    }

    memory_zero(cmd_page, PAGE_SIZE);
    uint8_t *buffer = buffer_page;
    for (size_t i = 0; i < PAGE_SIZE; i++) {
        buffer[i] = 0xccu;
    }

    uint8_t *cmd_bytes = cmd_page;
    accel_cmd_t *cmd = (accel_cmd_t *)(void *)(cmd_bytes + 64u);
    uint8_t *dst = buffer + 37u;
    const uint32_t len = 64u;

    if (accel_reset() != ACCEL_OK) {
        PANIC("accelerator descriptor reset failed");
    }

    scenario_init_memset_cmd(cmd, dst, len, 0x1234565au);
    if (accel_submit_sync(cmd) != ACCEL_OK ||
        cmd->status != ACCEL_CMD_STATUS_OK) {
        PANIC("accelerator descriptor memset failed");
    }
    expect_accel_status(ACCEL_STATUS_DONE, "descriptor-memset");
    expect_accel_irq_status(ACCEL_IRQ_DONE, "descriptor-memset");

    if (buffer[36] != 0xccu || buffer[37u + len] != 0xccu) {
        PANIC("accelerator descriptor memset overflowed buffer");
    }
    for (uint32_t i = 0; i < len; i++) {
        if (dst[i] != 0x5au) {
            PANIC("accelerator descriptor memset wrote wrong value");
        }
    }
    console_write("accel: descriptor memset passed\n");

    if (accel_submit_sync(cmd) != ACCEL_ERR_BUSY ||
        cmd->status != ACCEL_CMD_STATUS_REJECTED) {
        PANIC("accelerator descriptor lifecycle rejection failed");
    }

    if (accel_ack_irq(ACCEL_IRQ_DONE) != ACCEL_OK ||
        accel_reset() != ACCEL_OK) {
        PANIC("accelerator descriptor cleanup failed");
    }

    scenario_init_memset_cmd(cmd, dst, len, 0x11u);
    cmd->op = 99u;
    if (accel_submit_sync(cmd) != ACCEL_ERR_INVALID ||
        cmd->status != ACCEL_CMD_STATUS_INVALID) {
        PANIC("accelerator descriptor invalid op accepted");
    }
    expect_accel_status(ACCEL_STATUS_IDLE, "invalid-op");
    expect_accel_irq_status(0, "invalid-op");

    scenario_init_memset_cmd(cmd, buffer + PAGE_SIZE - 8u, 16u, 0x22u);
    if (accel_submit_sync(cmd) != ACCEL_ERR_INVALID ||
        cmd->status != ACCEL_CMD_STATUS_INVALID) {
        PANIC("accelerator descriptor page-crossing buffer accepted");
    }
    expect_accel_status(ACCEL_STATUS_IDLE, "crossing-buffer");
    expect_accel_irq_status(0, "crossing-buffer");

    scenario_init_memset_cmd(&static_accel_cmd, dst, len, 0x33u);
    if (accel_submit_sync(&static_accel_cmd) != ACCEL_ERR_INVALID ||
        static_accel_cmd.status != ACCEL_CMD_STATUS_ERROR) {
        PANIC("accelerator descriptor unmanaged descriptor accepted");
    }

    accel_cmd_t *unaligned_cmd = (accel_cmd_t *)(void *)(cmd_bytes + 1u);
    if (accel_submit_sync(unaligned_cmd) != ACCEL_ERR_INVALID) {
        PANIC("accelerator descriptor unaligned descriptor accepted");
    }

    accel_cmd_t *crossing_cmd =
        (accel_cmd_t *)(void *)(cmd_bytes + PAGE_SIZE - 16u);
    if (accel_submit_sync(crossing_cmd) != ACCEL_ERR_INVALID) {
        PANIC("accelerator descriptor page-crossing descriptor accepted");
    }
    console_write("accel: descriptor validation passed\n");
    console_write("accel: descriptor lifecycle rejection passed\n");
    console_write("milestone 19: accelerator descriptors\n");

    scenario_idle_forever();
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

    if (CONFIG_SCENARIO == SCENARIO_USER_SPACE) {
        scenario_user_space();
    }

    if (CONFIG_SCENARIO == SCENARIO_FIRST_USER) {
        scenario_first_user();
    }

    if (CONFIG_SCENARIO == SCENARIO_USERCOPY) {
        scenario_usercopy();
    }

    if (CONFIG_SCENARIO == SCENARIO_SCHEDULER_SYNC) {
        scenario_scheduler_sync();
    }

    if (CONFIG_SCENARIO == SCENARIO_DRIVER_FRAMEWORK) {
        scenario_driver_framework();
    }

    if (CONFIG_SCENARIO == SCENARIO_ACCEL_REGISTERS) {
        scenario_accel_registers();
    }

    if (CONFIG_SCENARIO == SCENARIO_ACCELERATOR_DESCRIPTORS) {
        scenario_accelerator_descriptors();
    }

    PANIC("unknown kernel scenario");
}
