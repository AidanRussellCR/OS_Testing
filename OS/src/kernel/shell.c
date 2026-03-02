#include <stddef.h>
#include <stdint.h>
#include "kernel/shell.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "kernel/sched.h"
#include "kernel/task.h"
#include "lib/str.h"
#include "ui/overlays.h"
#include "arsc/i386/ports.h"

#define INPUT_MAX 128

static void prompt(void) {
	terminal_write("> ");
	vga_cursor_set_pos(terminal_get_row(), terminal_get_col());
}

__attribute__((noreturn))
static void shutdown_machine(void) {
	outw(0x604, 0x2000);
	outw(0xB004, 0x2000);
	outw(0x4004, 0x3400);
	for (;;) __asm__ volatile ("cli; hlt");
}

static void read_line(char* out, size_t out_cap) {
	size_t len = 0;
	size_t cur = 0;

	size_t input_row = terminal_get_row();
	size_t input_col = terminal_get_col();

	out[0] = '\0';
	vga_cursor_set_pos(input_row, input_col);

	for (;;) {
		key_event_t ev;

		if (!keyboard_try_get_key(&ev)) {
			yield();
			continue;
		}

		if (ev.type == KEY_ENTER) {
			out[len] = '\0';
			terminal_set_cursor_pos(input_row, input_col + len);
			terminal_putc('\n');
			return;
		}

		if (ev.type == KEY_LEFT) {
			if (cur > 0) cur--;
		} else if (ev.type == KEY_RIGHT) {
			if (cur < len) cur++;
		} else if (ev.type == KEY_BACKSPACE) {
			if (cur > 0) {
				for (size_t i = cur - 1; i < len; i++) out[i] = out[i + 1];
				cur--;
				len--;
			}
		} else if (ev.type == KEY_DELETE) {
			if (cur < len) {
				for (size_t i = cur; i < len; i++) out[i] = out[i + 1];
				len--;
			}
		} else if (ev.type == KEY_CHAR) {
			if (len + 1 < out_cap) {
				for (size_t i = len; i > cur; i--) out[i] = out[i - 1];
				out[cur] = ev.ch;
				cur++;
				len++;
				out[len] = '\0';
			}
		}

		for (size_t i = 0; i < len; i++) {
			terminal_putc_at(input_row, input_col + i, out[i]);
		}
		for (size_t i = len; i < out_cap - 1 && (input_col + i) < VGA_WIDTH; i++) {
			terminal_putc_at(input_row, input_col + i, ' ');
		}

		vga_cursor_set_pos(input_row, input_col + cur);
	}
}

void task_shell(void) {
	char buf[INPUT_MAX];

	for (;;) {
		prompt();
		read_line(buf, sizeof(buf));

		if (streq(buf, "thanks")) {
			terminal_write("You're welcome!\n");
		} else if (streq(buf, "exit")) {
			terminal_write("Shutting down...\n");
			shutdown_machine();
		} else if (streq(buf, "clear")) {
			terminal_clear_text_area();
			overlays_redraw();
		} else if (streq(buf, "ps")) {
			task_print_to_console();
		} else if (starts_with(buf, "kill ")) {
			uint32_t id;
			if (parse_u32(buf + 5, &id) && task_kill((int)id)) {
				terminal_write("Killed task.\n");
			} else {
				terminal_write("Usage: kill <id>\n");
			}
		} else if (streq(buf, "spawn hb0")) {
			int id = task_create(task_heartbeat0, "heartbeat0");
			if (id >= 0) terminal_write("Spawned hb0.\n");
			else terminal_write("No free task slots.\n");
		} else if (streq(buf, "spawn hb1")) {
			int id = task_create(task_heartbeat1, "heartbeat1");
			if (id >= 0) terminal_write("Spawned hb1.\n");
			else terminal_write("No free task slots.\n");
		} else if (streq(buf, "yield")) {
			terminal_write("(yield)\n");
			yield();
		} else {
			terminal_write("Unknown command. Try: clear, ps, spawn hb0, spawn hb1, kill <id>\n");
		}

		yield();
	}
}
