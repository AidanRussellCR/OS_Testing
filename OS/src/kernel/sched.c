#include <stdint.h>
#include "kernel/sched.h"
#include "kernel/task.h"
#include "ui/overlays.h"
#include "arsc/i386/ctx_switch.h"

void _task_internal_set_current(int id);
int  _task_internal_get_current(void);
task_t* _task_internal_get(int id);

void schedule(void) {
	int prev = _task_internal_get_current();
	task_t* prev_t = (prev >= 0) ? _task_internal_get(prev) : 0;

	if (prev_t && prev_t->state == TASK_RUNNING) {
		prev_t->state = TASK_READY;
	}

	int next = -1;
	for (int step = 1; step <= MAX_TASKS; step++) {
		int idx = (prev + step) % MAX_TASKS;
		task_t* t = _task_internal_get(idx);
		if (t && t->state == TASK_READY) {
			next = idx;
			break;
		}
	}

	if (next == -1) {
		if (prev_t) {
			prev_t->state = TASK_RUNNING;
			debug_hud_draw();
			return;
		}
		debug_hud_draw();
		return;
	}

	task_t* next_t = _task_internal_get(next);
	if (!next_t) {
		debug_hud_draw();
		return;
	}

	_task_internal_set_current(next);
	next_t->state = TASK_RUNNING;

	debug_hud_draw();

	if (prev == -1) {
		uint32_t dummy = 0;
		ctx_switch(&dummy, next_t->esp);
	} else if (prev != next) {
		ctx_switch(&prev_t->esp, next_t->esp);
	}
}

void yield(void) {
	schedule();
}
