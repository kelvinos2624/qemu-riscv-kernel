#ifndef KERNEL_CORE_SCENARIO_H
#define KERNEL_CORE_SCENARIO_H

#define SCENARIO_ALLOCATOR 1
#define SCENARIO_HEAP 2
#define SCENARIO_VM 3
#define SCENARIO_PAGE_FAULT 4
#define SCENARIO_SCHEDULER_SYNC 5

void scenario_run(void) __attribute__((noreturn));

#endif
