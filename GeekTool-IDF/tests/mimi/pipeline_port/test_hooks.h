#pragma once
#include <stddef.h>
#define TEST_STACK_SENTINEL 1234u
typedef struct {size_t cap_blocks,cap_bytes,queues,tasks,power_locks,held_power_locks;} test_resources_t;
void test_fail_allocations(unsigned cap_attempt,unsigned queue_attempt,unsigned task_attempt);
void test_fail_power_allocation(unsigned attempt);
test_resources_t test_resources(void);
unsigned test_done_overwrites(void);
unsigned test_current_job(void);
