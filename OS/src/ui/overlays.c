#include <stddef.h>
#include <stdint.h>
#include "ui/overlays.h"
#include "drivers/vga.h"
#include "kernel/task.h"
#include "lib/str.h"

static int hud_dirty = 1;

void debug_hud_mark_dirty(void) { hud_dirty = 1; }

void overlay_clear_line(size_t row) {
	for (size_t c = HB_COL; c < VGA_WIDTH; c++) {
		terminal_putc_at(row, c, ' ');
	}
}

void debug_hud_draw(void) {
	if (!hud_dirty) return;
	hud_dirty = 0;

	const size_t hud_w = 26;
	const size_t hud_h = 6;
	const size_t start_col = VGA_WIDTH - hud_w;
	const size_t start_row = VGA_HEIGHT - hud_h;

	for (size_t r = 0; r < hud_h; r++) {
		for (size_t c = 0; c < hud_w; c++) {
			terminal_putc_at(start_row + r, start_col + c, ' ');
		}
	}

	terminal_write_at(start_row + 0, start_col, "Tasks");

	task_t* tasks = task_table();

	size_t line = 1;
	for (int i = 0; i < MAX_TASKS && line < hud_h; i++) {
		if (tasks[i].state == TASK_DEAD) continue;

		terminal_putc_at(start_row + line, start_col + 0, '#');
		terminal_putc_at(start_row + line, start_col + 1, '0' + (i % 10));
		terminal_putc_at(start_row + line, start_col + 2, ' ');
		terminal_putc_at(start_row + line, start_col + 3, task_state_char(tasks[i].state));
		terminal_putc_at(start_row + line, start_col + 4, ' ');

		const char* nm = tasks[i].name ? tasks[i].name : "?";
		size_t col = start_col + 5;
		for (size_t k = 0; nm[k] && col < VGA_WIDTH; k++, col++) {
			terminal_putc_at(start_row + line, col, nm[k]);
		}
		line++;
	}
}

void overlays_redraw(void) {
	for (size_t r = 0; r < (HB1_ROW_BASE + HB_MAX_LINES); r++) {
		overlay_clear_line(r);
	}
	debug_hud_mark_dirty();
	debug_hud_draw();
}

void task_heartbeat0(void) {
	uint32_t n = 0;
	for (;;) {
		int me = task_current_id();
		int idx = hb_instance_index("heartbeat0", me);
		if (idx >= 0 && idx < HB_MAX_LINES) {
			size_t row = HB0_ROW_BASE + (size_t)idx;
			overlay_clear_line(row);
			terminal_write_at(row, HB_COL, "HB0 #");
			terminal_putc_at(row, HB_COL + 5, '0' + (me % 10));
			terminal_write_at(row, HB_COL + 6, " : ");
			terminal_putc_at(row, HB_COL + 9, '0' + (n % 10));
		}
		n++;
		task_delay(800000);
	}
}

void task_heartbeat1(void) {
	uint32_t n = 0;
	for (;;) {
		int me = task_current_id();
		int idx = hb_instance_index("heartbeat1", me);
		if (idx >= 0 && idx < HB_MAX_LINES) {
			size_t row = HB1_ROW_BASE + (size_t)idx;
			overlay_clear_line(row);
			terminal_write_at(row, HB_COL, "HB1 #");
			terminal_putc_at(row, HB_COL + 5, '0' + (me % 10));
			terminal_write_at(row, HB_COL + 6, " : ");
			terminal_putc_at(row, HB_COL + 9, '0' + (n % 10));
		}
		n++;
		task_delay(1100000);
	}
}
