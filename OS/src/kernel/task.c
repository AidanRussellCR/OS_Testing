#include <stdint.h>
#include "kernel/task.h"
#include "drivers/vga.h"
#include "ui/overlays.h"
#include "lib/str.h"
#include "kernel/sched.h"

#define STACK_SIZE 4096
static uint8_t stacks[MAX_TASKS][STACK_SIZE] __attribute__((aligned(16)));

static task_t tasks[MAX_TASKS];
static int current_task = -1;

static void task_trampoline(void);

task_t* task_table(void) { return tasks; }

int task_current_id(void) { return current_task; }
static void task_set_current(int id) { current_task = id; }

static int task_alloc_slot(void) {
	for (int i = 0; i < MAX_TASKS; i++) {
		if (tasks[i].state == TASK_DEAD) return i;
	}
	return -1;
}

void task_init(void) {
	for (int i = 0; i < MAX_TASKS; i++) {
		tasks[i].state = TASK_DEAD;
		tasks[i].name = 0;
		tasks[i].entry = 0;
		tasks[i].esp = 0;
	}
	current_task = -1;
	debug_hud_mark_dirty();
}

int task_kill(int id) {
	if (id < 0 || id >= MAX_TASKS) return 0;
	if (tasks[id].state == TASK_DEAD) return 0;
	if (id == current_task) return 0;

	if (tasks[id].name && streq(tasks[id].name, "heartbeat0")) {
		int idx = hb_instance_index("heartbeat0", id);
		if (idx >= 0 && idx < HB_MAX_LINES) overlay_clear_line(HB0_ROW_BASE + (size_t)idx);
	}
	if (tasks[id].name && streq(tasks[id].name, "heartbeat1")) {
		int idx = hb_instance_index("heartbeat1", id);
		if (idx >= 0 && idx < HB_MAX_LINES) overlay_clear_line(HB1_ROW_BASE + (size_t)idx);
	}

	tasks[id].state = TASK_DEAD;
	tasks[id].entry = 0;
	tasks[id].name = 0;
	tasks[id].esp = 0;

	debug_hud_mark_dirty();
	return 1;
}

void task_exit(void) {
	for (;;) yield();
}

static void task_trampoline(void) {
	void (*fn)(void) = tasks[current_task].entry;
	fn();
	task_exit();
}

int task_create(void (*entry)(void), const char* name) {
	int id = task_alloc_slot();
	if (id < 0) return -1;

	task_t* t = &tasks[id];
	t->entry = entry;
	t->state = TASK_READY;
	t->name  = name;

	uint32_t sp = (uint32_t)&stacks[id][STACK_SIZE];

	sp -= 4; *(uint32_t*)sp = (uint32_t)task_trampoline;	/* ret */
	sp -= 4; *(uint32_t*)sp = 0x00000002;			/* eflags */

	sp -= 4; *(uint32_t*)sp = 0; /* eax */
	sp -= 4; *(uint32_t*)sp = 0; /* ecx */
	sp -= 4; *(uint32_t*)sp = 0; /* edx */
	sp -= 4; *(uint32_t*)sp = 0; /* ebx */
	sp -= 4; *(uint32_t*)sp = 0; /* esp dummy */
	sp -= 4; *(uint32_t*)sp = 0; /* ebp */
	sp -= 4; *(uint32_t*)sp = 0; /* esi */
	sp -= 4; *(uint32_t*)sp = 0; /* edi */

	t->esp = sp;

	debug_hud_mark_dirty();
	return id;
}

char task_state_char(task_state_t s) {
	switch (s) {
		case TASK_READY:   return 'R';
		case TASK_RUNNING: return '*';
		case TASK_BLOCKED: return 'B';
		case TASK_DEAD:    return 'D';
		default:           return '?';
	}
}

void task_print_to_console(void) {
	terminal_write("ID STATE NAME\n");
	for (int i = 0; i < MAX_TASKS; i++) {
		if (tasks[i].state == TASK_DEAD) continue;

		terminal_putc('0' + (i % 10));
		terminal_write("  ");
		terminal_putc(task_state_char(tasks[i].state));
		terminal_write("     ");
		terminal_write(tasks[i].name ? tasks[i].name : "?");
		terminal_putc('\n');
	}
}

int hb_instance_index(const char* hb_name, int my_id) {
	int idx = 0;
	for (int i = 0; i < MAX_TASKS; i++) {
		if (tasks[i].state == TASK_DEAD) continue;
		if (!tasks[i].name) continue;
		if (!streq(tasks[i].name, hb_name)) continue;

		if (i == my_id) return idx;
		idx++;
	}
	return -1;
}

void task_delay(volatile uint32_t loops) {
	for (volatile uint32_t i = 0; i < loops; i++) {
		__asm__ volatile ("pause");
		if ((i & 0x3FFF) == 0) yield();
	}
}

void _task_internal_set_current(int id) { task_set_current(id); }
int  _task_internal_get_current(void)   { return current_task; }
