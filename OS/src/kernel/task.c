#include <stdint.h>
#include <stddef.h>
#include "kernel/task.h"
#include "kernel/sched.h"
#include "drivers/vga.h"
#include "lib/str.h"
#include "mm/heap.h"
#include "ui/overlays.h"


#define KSTACK_SIZE 16384

static task_t* g_tasks[MAX_TASKS];
static int g_current = -1;

static void task_trampoline(void);

task_t* task_at(int id) {
    if (id < 0 || id >= MAX_TASKS) return 0;
	return g_tasks[id];
}

int task_current_id(void) { return g_current; }

static int alloc_slot(void) {
	for (int i = 0; i < MAX_TASKS; i++) {
		if (g_tasks[i] == 0) return i;
	}
	return -1;
}

void task_init(void) {
	for (int i = 0; i < MAX_TASKS; i++) g_tasks[i] = 0;
	g_current = -1;
}

static void build_initial_context(task_t* t) {
	uint8_t* base = (uint8_t*)t->kstack_base;
	uint32_t sp = (uint32_t)(base + t->kstack_size);

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
}

int task_create(void (*entry)(void), const char* name) {
	int id = alloc_slot();
	if (id < 0) return -1;

	task_t* t = (task_t*)kmalloc(sizeof(task_t));
	if (!t) return -1;

	void* stack = kmalloc(KSTACK_SIZE);
	if (!stack) {
		kfree(t);
		return -1;
	}

	t->entry = entry;
	t->name = name;
	t->state = TASK_READY;
	t->kstack_base = stack;
	t->kstack_size = KSTACK_SIZE;
	t->esp = 0;

	build_initial_context(t);

	g_tasks[id] = t;

	return id;
}

static void cleanup_task_slot(int id) {
	task_t* t = g_tasks[id];
	if (!t) return;

	overlays_hb_remove(id);

	kfree(t->kstack_base);
	kfree(t);
	g_tasks[id] = 0;
}

int task_kill(int id) {
	if (id < 0 || id >= MAX_TASKS) return 0;
	task_t* t = g_tasks[id];
	if (!t) return 0;

	// never kill current task from shell
	if (id == g_current) return 0;

	// cannot kill shell or wraith tasks
	if (t->name && (streq(t->name, "shell") || streq(t->name, "wraith"))) return 0;

	t->state = TASK_ZOMBIE;
	return 1;
}

void task_exit(void) {
	for (;;) yield();
}

static void task_trampoline(void) {
	task_t* t = g_tasks[g_current];
	void (*fn)(void) = t ? t->entry : 0;
	if (fn) fn();
	task_exit();
}

char task_state_char(task_state_t s) {
	switch (s) {
		case TASK_READY:   return 'R';
		case TASK_RUNNING: return '*';
		case TASK_BLOCKED: return 'B';
		case TASK_DEAD:	return 'D';
		default: return '?';
	}
}

void task_print_to_console(void) {
	terminal_write("ID STATE NAME\n");
	for (int i = 0; i < MAX_TASKS; i++) {
		task_t* t = g_tasks[i];
		if (!t) continue;

		// print ID
		char tmp[12];
		int p = 0;
		uint32_t v = (uint32_t)i;

		if (v == 0) tmp[p++] = '0';
		else {
			char r[12];
			int rp = 0;
			while (v > 0 && rp < 11) { r[rp++] = (char)('0' + (v % 10)); v /= 10; }
			while (rp > 0) tmp[p++] = r[--rp];
		}
		tmp[p] = '\0';

		terminal_write(tmp);
		terminal_write("  ");
		terminal_putc(task_state_char(t->state));
		terminal_write("     ");
		terminal_write(t->name ? t->name : "?");
		terminal_putc('\n');
	}
}

int hb_instance_index(const char* hb_name, int my_id) {
	int idx = 0;
	for (int i = 0; i < MAX_TASKS; i++) {
		task_t* t = g_tasks[i];
		if (!t) continue;
		if (!t->name) continue;
		if (!streq(t->name, hb_name)) continue;
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

void task_wraith(void) {
	for (;;) {
		// reap zombies
		for (int i = 0; i < MAX_TASKS; i++) {
			task_t* t = g_tasks[i];
			if (!t) continue;
			if (t->state != TASK_ZOMBIE) continue;

			// don't reap shell or wraith by mistake
			if (t->name && (streq(t->name, "shell") || streq(t->name, "wraith"))) {
				t->state = TASK_READY;
				continue;
			}

			cleanup_task_slot(i);
		}

		// wait
		task_delay(200000);
	}
}

// for scheduler
void _task_internal_set_current(int id) { g_current = id; }
int  _task_internal_get_current(void)   { return g_current; }
task_t* _task_internal_get(int id)	{ return task_at(id); }

