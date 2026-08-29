#ifndef KERNEL_CORE_SCENARIO_H
#define KERNEL_CORE_SCENARIO_H

#define SCENARIO_ALLOCATOR 1
#define SCENARIO_HEAP 2
#define SCENARIO_VM 3
#define SCENARIO_PAGE_FAULT 4
#define SCENARIO_USER_SPACE 5
#define SCENARIO_FIRST_USER 6
#define SCENARIO_USERCOPY 7
#define SCENARIO_SCHEDULER_SYNC 8

void scenario_run(void) __attribute__((noreturn));

#endif
