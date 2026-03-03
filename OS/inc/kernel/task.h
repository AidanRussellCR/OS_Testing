#pragma once
#include <stdint.h>

typedef enum {
	TASK_DEAD = 0,
	TASK_READY,
	TASK_RUNNING,
	TASK_BLOCKED
} task_state_t;

typedef struct task {
	uint32_t esp;
	task_state_t state;
	const char* name;
	void (*entry)(void);
	void* kstack_base;
	uint32_t kstack_size;
} task_t;

// Cap right now for tracked tasks
#define MAX_TASKS 64

void task_init(void);
int task_create(void (*entry)(void), const char* name);
int task_kill(int id);
int task_current_id(void);
task_t* task_at(int id);

void task_exit(void) __attribute__((noreturn));
void task_delay(volatile uint32_t loops);

char task_state_char(task_state_t s);
void task_print_to_console(void);
int hb_instance_index(const char* hb_name, int my_id);

