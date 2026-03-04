#include <stddef.h>
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
#include "fs/vfs.h"
#include "mm/heap.h"

static void prompt(void) {
	char path[96];
	vfs_pwd(path, sizeof(path));
	terminal_write(path);
	terminal_write(" > ");
	terminal_set_cursor_pos(terminal_get_row(), terminal_get_col());
}

__attribute__((noreturn))
static void shutdown_machine(void) {
	outw(0x604, 0x2000);
	outw(0xB004, 0x2000);
	outw(0x4004, 0x3400);
	for (;;) __asm__ volatile ("cli; hlt");
}

static void pos_to_rc(size_t start_row, size_t start_col, size_t pos, size_t* out_r, size_t* out_c) {
	size_t linear = start_col + pos;
	*out_r = start_row + (linear / VGA_WIDTH);
	*out_c = linear % VGA_WIDTH;
}

static size_t input_capacity_on_screen(size_t start_row, size_t start_col) {
	// number of character cells between prompt cursor to bottom of text
	if (start_row >= TERM_HEIGHT) return 0;
	size_t rows_avail = TERM_HEIGHT - start_row;
	return rows_avail * VGA_WIDTH - start_col;
}

static void redraw_input_region(size_t start_row, size_t start_col, const char* buf, size_t len, size_t prev_len) {
	size_t cap = input_capacity_on_screen(start_row, start_col);
	size_t max = prev_len > len ? prev_len : len;
	if (max > cap) max = cap;

	for (size_t i = 0; i < max; i++) {
		size_t r, c;
		pos_to_rc(start_row, start_col, i, &r, &c);
		char ch = (i < len) ? buf[i] : ' ';
		terminal_putc_at(r, c, ch);
	}
}

static char* read_line(void) {
	size_t start_row = terminal_get_row();
	size_t start_col = terminal_get_col();

	size_t cap = 64;
	size_t len = 0;
	size_t cur = 0;
	size_t prev_len = 0;

	char* buf = (char*)kmalloc(cap);
	if (!buf) return 0;
	buf[0] = '\0';

	// open cells until the overlay rows (hb0/1)
	size_t screen_cap = input_capacity_on_screen(start_row, start_col);
	if (screen_cap == 0) {
		kfree(buf);
		return 0;
	}

	for (;;) {
		key_event_t ev;

		if (!keyboard_try_get_key(&ev)) {
			yield();
			continue;
		}

		if (ev.type == KEY_LEFT) {
			if (cur > 0) cur--;
		} else if (ev.type == KEY_RIGHT) {
			if (cur < len) cur++;
		} else if (ev.type == KEY_DELETE) {
			if (cur < len) {
				for (size_t i = cur; i + 1 <= len; i++) buf[i] = buf[i + 1];
				len--;
			}
		} else if (ev.type == KEY_BACKSPACE) {
			if (cur > 0) {
				for (size_t i = cur - 1; i + 1 <= len; i++) buf[i] = buf[i + 1];
				cur--;
				len--;
			}
		} else if (ev.type == KEY_ENTER) {
			// move cursor to end of input and go to next line
			size_t end_r, end_c;
			pos_to_rc(start_row, start_col, len, &end_r, &end_c);
			if (end_r >= TERM_HEIGHT) end_r = TERM_HEIGHT - 1;
			terminal_set_cursor_pos(end_r, end_c);

			terminal_putc('\n');
			buf[len] = '\0';
			return buf;
		} else if (ev.type == KEY_CHAR) {
			if (len + 1 >= screen_cap) {
				// ignore extra chars if available space is full
			} else {
				// grow heap buffer if needed
				if (len + 1 >= cap) {
					size_t new_cap = cap * 2;
					char* nb = (char*)kmalloc(new_cap);
					if (!nb) {
						// ignore if OOM
					} else {
						for (size_t i = 0; i < len; i++) nb[i] = buf[i];
						nb[len] = '\0';
						kfree(buf);
						buf = nb;
						cap = new_cap;
					}
				}

				// insert at cursor
				if (len + 1 < cap) {
					for (size_t i = len + 1; i > cur; i--) buf[i] = buf[i - 1];
					buf[cur] = ev.ch;
					cur++;
					len++;
					buf[len] = '\0';
				}
			}
		}

		// redraw input and place cursor
		redraw_input_region(start_row, start_col, buf, len, prev_len);
		prev_len = len;

		size_t r, c;
		pos_to_rc(start_row, start_col, cur, &r, &c);
		if (r >= TERM_HEIGHT) r = TERM_HEIGHT - 1;
		terminal_set_cursor_pos(r, c);
	}
}

static char read_yes_no(void) {
	for (;;) {
		key_event_t ev;
		if (!keyboard_try_get_key(&ev)) { yield(); continue; }
		if (ev.type == KEY_CHAR) {
			char c = ev.ch;
			if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
			if (c == 'y' || c == 'n') {
				terminal_putc(c);
				terminal_putc('\n');
				return c;
			}
		}
	}
}

static void vfs_print_status(vfs_status_t st) {
	switch (st) {
		case VFS_OK: terminal_write("Done.\n"); break;
		case VFS_ERR_NOT_FOUND: terminal_write("Not found.\n"); break;
		case VFS_ERR_EXISTS: terminal_write("Already exists.\n"); break;
		case VFS_ERR_NOT_DIR: terminal_write("Not a directory.\n"); break;
		case VFS_ERR_IS_DIR: terminal_write("Is a directory.\n"); break;
		case VFS_ERR_NAME_INVALID: terminal_write("Invalid name.\n"); break;
		case VFS_ERR_NO_MEM: terminal_write("Out of memory.\n"); break;
		case VFS_ERR_BUSY: terminal_write("Busy.\n"); break;
		default: terminal_write("Error.\n"); break;
	}
}

static void shop_print_cb(const char* name, int is_dir, void* user) {
	(void)user;
	if (is_dir) {
		terminal_write("[DIR]  ");
		terminal_write(name);
		terminal_putc('\n');
	} else {
		terminal_write("       ");
		terminal_write(name);
		terminal_putc('\n');
	}
}

void task_shell(void) {
	for (;;) {
		prompt();
		
		char* buf = read_line();
		
		if (!buf) {
			terminal_write("Out of memory.\n");
			yield();
			continue;
		}

		if (streq(buf, "thanks")) {
			terminal_write("You're welcome!\n");
		} else if (streq(buf, "sync")) {
			if (!vfs_is_dirty()) {
				terminal_write("File system is clean.\n");
			} else {
				vfs_status_t s = vfs_save();
				if (s == VFS_OK) terminal_write("Saved to disk.\n");
				else terminal_write("Save failed.\n");
			}
		} else if (streq(buf, "exit")) {
			terminal_write("Shutting down...\n");
			if (vfs_is_dirty()) vfs_save();
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
		} else if (streq(buf, "shop")) {
			terminal_write("In this directory:\n");
			vfs_shop(shop_print_cb, 0);
		} else if (starts_with(buf, "fab ")) {
			vfs_status_t st = vfs_fab(buf + 4);
			vfs_print_status(st);
			if (vfs_is_dirty()) vfs_save();
		} else if (starts_with(buf, "insp ")) {
			const char* text = 0;
			vfs_status_t st = vfs_insp(buf + 5, &text);
			if (st != VFS_OK) {
				vfs_print_status(st);
			} else {
				terminal_write(text ? text : "");
				terminal_putc('\n');
			}
		} else if (starts_with(buf, "carve ")) {
			// carve <text string> :: <filename>
			const char* rest = buf + 6;
			const char* delim = kstrstr(rest, " :: ");
			if (!delim) delim = kstrstr(rest, "::");
			if (!delim) {
				terminal_write("Usage: carve <text> :: <filename>\n");
			} else {
				size_t text_len = (size_t)(delim - rest);
				while (text_len > 0 && (rest[text_len - 1] == ' ')) text_len--;

				const char* fname = delim;
				if (kstrncmp(delim, " :: ", 4) == 0) fname = delim + 4;
				else fname = delim + 2;
				while (*fname == ' ') fname++;

				char* text = (char*)kmalloc(text_len + 1);
				if (!text) {
					terminal_write("Out of memory.\n");
				} else {
					for (size_t i = 0; i < text_len; i++) text[i] = rest[i];
					text[text_len] = '\0';
					vfs_status_t st = vfs_carve(fname, text);
					kfree(text);
					vfs_print_status(st);
				}
			}
			if (vfs_is_dirty()) vfs_save();
		} else if (starts_with(buf, "burn ")) {
			terminal_write("Burn file '");
			terminal_write(buf + 5);
			terminal_write("'? (y/n): ");
			char yn = read_yes_no();
			if (yn == 'y') {
				vfs_status_t st = vfs_burn(buf + 5);
				vfs_print_status(st);
				if (vfs_is_dirty()) vfs_save();
			} else {
				terminal_write("Canceled.\n");
			}
		} else if (starts_with(buf, "newdir ")) {
			vfs_status_t st = vfs_mkdir(buf + 7);
			vfs_print_status(st);
			if (vfs_is_dirty()) vfs_save();
		} else if (starts_with(buf, "cd ")) {
			vfs_status_t st = vfs_cd(buf + 3);
			vfs_print_status(st);
		} else {
			terminal_write("Unknown command. Try: clear, ps, spawn hb0, spawn hb1, kill <id>\n");
			terminal_write("FS: fab <file>, insp <file>, carve <text> :: <file>, burn <file>, newdir <dir>, cd <dir>, cd ..\n");
		}

		kfree(buf);
		yield();
	}
}

