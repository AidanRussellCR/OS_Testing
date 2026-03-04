#include <stddef.h>
#include <stdint.h>
#include "ui/overlays.h"
#include "drivers/vga.h"
#include "lib/str.h"

#include "kernel/task.h"
#include "kernel/sched.h"

#define MAX_TRACK 64	// same as max tasks
#define MAX_SHOW  10	// number of tasks to display at a time

static uint8_t  g_hb_active[2][MAX_TRACK];
static uint32_t g_hb_count[2][MAX_TRACK];

static void clear_overlay_rows(void) {
	terminal_clear_row(OVERLAY_ROW0);
	terminal_clear_row(OVERLAY_ROW1);
}

static size_t write_u32_at(size_t row, size_t col, uint32_t v) {
	char buf[12];
	int i = 0;
	if (v == 0) { buf[i++] = '0'; }
	else {
		char tmp[12];
		int t = 0;
		while (v > 0 && t < 11) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
		while (t > 0) buf[i++] = tmp[--t];
	}
	buf[i] = '\0';
	terminal_write_at(row, col, buf);
	return (size_t)i;
}

static void redraw_line(int hb_kind, size_t row) {
	terminal_clear_row(row);

	if (hb_kind == 0) terminal_write_at(row, 0, "HB0: ");
	else              terminal_write_at(row, 0, "HB1: ");

	size_t col = 5;
	int shown = 0;

	for (int id = 0; id < MAX_TRACK && shown < MAX_SHOW; id++) {
		if (!g_hb_active[hb_kind][id]) continue;

		// format is [id:count]
		if (col + 1 >= VGA_WIDTH) break;
		terminal_putc_at(row, col++, '[');

		col += write_u32_at(row, col, (uint32_t)id);

		if (col + 1 >= VGA_WIDTH) break;
		terminal_putc_at(row, col++, ':');

		col += write_u32_at(row, col, g_hb_count[hb_kind][id]);

		if (col + 2 >= VGA_WIDTH) break;
		terminal_putc_at(row, col++, ']');
		terminal_putc_at(row, col++, ' ');

		shown++;
	}
}

void overlays_redraw(void) {
	clear_overlay_rows();
	redraw_line(0, OVERLAY_ROW0);
	redraw_line(1, OVERLAY_ROW1);
}

void overlays_hb_tick(int hb_kind, int task_id, uint32_t counter) {
	if (hb_kind < 0 || hb_kind > 1) return;
	if (task_id < 0 || task_id >= MAX_TRACK) return;

	g_hb_active[hb_kind][task_id] = 1;
	g_hb_count[hb_kind][task_id] = counter;

	// redraw rows when changes occur
	redraw_line(0, OVERLAY_ROW0);
	redraw_line(1, OVERLAY_ROW1);
}

void overlays_hb_remove(int task_id) {
	if (task_id < 0 || task_id >= MAX_TRACK) return;

	g_hb_active[0][task_id] = 0;
	g_hb_active[1][task_id] = 0;
	g_hb_count[0][task_id] = 0;
	g_hb_count[1][task_id] = 0;

	redraw_line(0, OVERLAY_ROW0);
	redraw_line(1, OVERLAY_ROW1);
}

void task_heartbeat0(void) {
	uint32_t n = 0;
	for (;;) {
		overlays_hb_tick(0, task_current_id(), n++);
		task_delay(800000);
	}
}

void task_heartbeat1(void) {
	uint32_t n = 0;
	for (;;) {
		overlays_hb_tick(1, task_current_id(), n++);
		task_delay(1100000);
	}
}

