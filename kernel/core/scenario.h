#ifndef KERNEL_CORE_SCENARIO_H
#define KERNEL_CORE_SCENARIO_H

#define SCENARIO_ALLOCATOR 1
#define SCENARIO_HEAP 2
#define SCENARIO_SCHEDULER_SYNC 3

void scenario_run(void) __attribute__((noreturn));

#endif
