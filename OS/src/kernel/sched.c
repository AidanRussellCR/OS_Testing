#include <stdint.h>
#include "kernel/sched.h"
#include "kernel/task.h"
#include "ui/overlays.h"
#include "arsc/i386/ctx_switch.h"

void _task_internal_set_current(int id);
int  _task_internal_get_current(void);

void schedule(void) {
	task_t* tasks = task_table();
	int prev = _task_internal_get_current();

	if (prev >= 0 && tasks[prev].state == TASK_RUNNING) {
		tasks[prev].state = TASK_READY;
	}

	int next = -1;
	for (int step = 1; step <= MAX_TASKS; step++) {
		int idx = (prev + step) % MAX_TASKS;
		if (tasks[idx].state == TASK_READY) {
			next = idx;
			break;
		}
	}

	if (next == -1) {
		if (prev >= 0 && tasks[prev].state != TASK_DEAD) {
			tasks[prev].state = TASK_RUNNING;
			debug_hud_draw();
			return;
		}
		debug_hud_draw();
		return;
	}

	_task_internal_set_current(next);
	tasks[next].state = TASK_RUNNING;

	debug_hud_draw();

	if (prev == -1) {
		uint32_t dummy = 0;
		ctx_switch(&dummy, tasks[next].esp);
	} else if (prev != next) {
		ctx_switch(&tasks[prev].esp, tasks[next].esp);
	}
}

void yield(void) {
	schedule();
}
