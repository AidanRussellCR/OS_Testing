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

#define HISTORY_MAX 32
#define SCRIPT_DEPTH_MAX 4

static char g_history[HISTORY_MAX][160];
static int g_history_count = 0;
static void shell_execute_command(const char* buf, int from_script, int depth);
static char* read_line(void);
static char read_yes_no(void);
static void vfs_print_status(vfs_status_t st);
static int split_next_line(const char** p, char* out, size_t cap);
static void shell_execute_command(const char* buf, int from_script, int depth);
static void shell_cast_script(const char* filename, int depth);

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

static void history_push(const char* line) {
	if (!line || !line[0]) return;

	// avoid duplicate consecutive entries
	if (g_history_count > 0 && streq(g_history[(g_history_count - 1) % HISTORY_MAX], line)) return;

	if (g_history_count < HISTORY_MAX) {
		kstrncpy0(g_history[g_history_count], line, sizeof(g_history[g_history_count]));
		g_history_count++;
	} else {
		for (int i = 1; i < HISTORY_MAX; i++) {
			kstrcpy(g_history[i - 1], g_history[i]);
		}
		kstrncpy0(g_history[HISTORY_MAX - 1], line, sizeof(g_history[HISTORY_MAX - 1]));
	}
}

static void shell_cast_script(const char* filename, int depth) {
	if (depth >= SCRIPT_DEPTH_MAX) {
		terminal_write("Spell recursion too deep.\n");
		return;
	}

	int learned = 0;
	vfs_status_t st = vfs_is_learned(filename, &learned);
	if (st != VFS_OK) {
		vfs_print_status(st);
		return;
	}
	if (!learned) {
		terminal_write("That spell is not learned.\n");
		return;
	}

	const char* text = 0;
	st = vfs_insp(filename, &text);
	if (st != VFS_OK) {
		vfs_print_status(st);
		return;
	}

	const char* p = text ? text : "";
	char line[160];

	while (split_next_line(&p, line, sizeof(line))) {
		if (!line[0]) continue;

		terminal_write("[");
		terminal_write(line);
		terminal_write("] :\n");

		shell_execute_command(line, 1, depth + 1);
	}
}

static int split_next_line(const char** p, char* out, size_t cap) {
	if (!p || !*p || !**p) return 0;

	size_t i = 0;
	while (**p && **p != '\n' && i + 1 < cap) {
		if (**p != '\r') out[i++] = **p;
		(*p)++;
	}
	out[i] = '\0';

	if (**p == '\n') (*p)++;
	return 1;
}

static void pos_to_rc(size_t start_buf_row, size_t start_col, size_t pos, size_t* out_r, size_t* out_c) {
	size_t linear = start_col + pos;
	*out_r = start_buf_row + (linear / TEXT_WIDTH);
	*out_c = linear % TEXT_WIDTH;
}

static size_t input_capacity_on_screen(size_t start_buf_row, size_t start_col) {
	size_t used = start_buf_row * TEXT_WIDTH + start_col;
	size_t total = SCROLLBACK_ROWS * TEXT_WIDTH;

	if (used >= total) return 0;
	return total - used;
}

static void redraw_input_region(size_t start_buf_row, size_t start_col, const char* buf, size_t len, size_t prev_len) {
	size_t max = (prev_len > len) ? prev_len : len;
	size_t view_top = terminal_get_view_top();

	for (size_t i = 0; i < max; i++) {
		size_t r, c;
		pos_to_rc(start_buf_row, start_col, i, &r, &c);

		// redraw visible rows
		if (r < view_top || r >= view_top + TERM_HEIGHT) continue;

		char ch = (i < len) ? buf[i] : ' ';
		terminal_putc_at(r - view_top, c, ch);
	}
}

static void load_history_entry(char** pbuf, size_t* pcap, size_t* plen, size_t* pcur, const char* src, size_t screen_cap) {
	size_t n = kstrlen(src);
	if (n >= screen_cap) n = (screen_cap > 0) ? (screen_cap - 1) : 0;

	if (n + 2 >= *pcap) {
		size_t new_cap = *pcap;
		while (n + 2 >= new_cap) new_cap *= 2;
		char* nb = (char*)kmalloc(new_cap);
		if (!nb) return;
		kfree(*pbuf);
		*pbuf = nb;
		*pcap = new_cap;
	}

	for (size_t i = 0; i < n; i++) (*pbuf)[i] = src[i];
	(*pbuf)[n] = '\0';
	*plen = n;
	*pcur = n;
}

static char* read_line(void) {
	size_t start_row = terminal_get_buffer_row();
	size_t start_col = terminal_get_col();
	size_t total_cap = input_capacity_on_screen(start_row, start_col);

	size_t cap = 64;
	size_t len = 0;
	size_t cur = 0;
	size_t prev_len = 0;
	
	size_t history_index = (size_t)g_history_count;

	char* buf = (char*)kmalloc(cap);
	if (!buf) return 0;
	buf[0] = '\0';

	if (total_cap == 0) {
		kfree(buf);
		return 0;
	}

	for (;;) {
		key_event_t ev;

		if (!keyboard_try_get_key(&ev)) {
			yield();
			continue;
		}

		if (ev.type == KEY_PAGEUP) {
			terminal_scroll_view_up();
			continue;
		} else if (ev.type == KEY_PAGEDOWN) {
			terminal_scroll_view_down();
			continue;
		} else if (ev.type == KEY_UP) {
			if (g_history_count > 0 && history_index > 0) {
				history_index--;
				load_history_entry(&buf, &cap, &len, &cur, g_history[history_index], total_cap);
			}
		} else if (ev.type == KEY_DOWN) {
			if (history_index < (size_t)g_history_count) history_index++;
			if (history_index == (size_t)g_history_count) {
				len = 0;
				cur = 0;
				buf[0] = '\0';
			} else {
				load_history_entry(&buf, &cap, &len, &cur, g_history[history_index], total_cap);
			}
		} else if (ev.type == KEY_LEFT) {
			if (cur > 0) cur--;
		} else if (ev.type == KEY_RIGHT) {
			if (cur < len) cur++;
		} else if (ev.type == KEY_DELETE) {
			if (!terminal_is_following_tail()) terminal_follow_tail();
			
			if (cur < len) {
				for (size_t i = cur; i + 1 <= len; i++) buf[i] = buf[i + 1];
				len--;
			}
		} else if (ev.type == KEY_BACKSPACE) {
			if (!terminal_is_following_tail()) terminal_follow_tail();
			
			if (cur > 0) {
				for (size_t i = cur - 1; i + 1 <= len; i++) buf[i] = buf[i + 1];
				cur--;
				len--;
			}
		} else if (ev.type == KEY_ENTER) {
			if (!terminal_is_following_tail()) terminal_follow_tail();
			
			// move cursor to end of input and go to next line
			size_t end_r, end_c;
			pos_to_rc(start_row, start_col, len, &end_r, &end_c);

			terminal_ensure_row_visible(end_r);

			size_t view_top = terminal_get_view_top();
			if (end_r >= view_top && end_r < view_top + TERM_HEIGHT) {
				terminal_set_cursor_pos(end_r - view_top, end_c);
			}

			terminal_putc('\n');
			buf[len] = '\0';
			return buf;
		} else if (ev.type == KEY_CHAR) {
			if (!terminal_is_following_tail()) terminal_follow_tail();
			
			if (len + 1 >= total_cap) {
				// ignore extra chars if available space is full
			} else {
				// grow heap buffer if needed
				if (len + 2 >= cap) {
					size_t new_cap = cap * 2;
					char* nb = (char*)kmalloc(new_cap);
					if (nb) {
						for (size_t i = 0; i < len; i++) nb[i] = buf[i];
						nb[len] = '\0';
						kfree(buf);
						buf = nb;
						cap = new_cap;
					}
				}

				// insert at cursor
				if (len + 2 < cap) {
					for (size_t i = len + 1; i > cur; i--) buf[i] = buf[i - 1];
					buf[cur] = ev.ch;
					cur++;
					len++;
					buf[len] = '\0';
				}
			}
		}

		// redraw input and place cursor
		size_t r, c;
		pos_to_rc(start_row, start_col, cur, &r, &c);

		// keep current edit row visible if it goes past the last row
		terminal_ensure_row_visible(r);

		redraw_input_region(start_row, start_col, buf, len, prev_len);
		prev_len = len;

		size_t view_top = terminal_get_view_top();
		if (r >= view_top && r < view_top + TERM_HEIGHT) {
			terminal_set_cursor_pos(r - view_top, c);
		}
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
		case VFS_OK: terminal_write("It has been done.\n"); break;
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

static void grimoire_print_cb(const char* name, void* user) {
	(void)user;
	terminal_write(" * ");
	terminal_write(name);
	terminal_putc('\n');
}

static void shell_execute_command(const char* buf, int from_script, int depth) {
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
		if (from_script) {
			terminal_write("Scripts may not shut down the system.\n");
		} else {
			terminal_write("Shutting down...\n");
			if (vfs_is_dirty()) vfs_save();
			shutdown_machine();
		}
	} else if (streq(buf, "help")) {
		terminal_write("Commands:\n");
		terminal_write("  help                    - show this help\n");
		terminal_write("  clear                   - clear terminal text area\n");
		terminal_write("  ps                      - list running tasks\n");
		terminal_write("  kill <id>               - mark a task for reaping\n");
		terminal_write("  spawn hb0               - spawn heartbeat type 0\n");
		terminal_write("  spawn hb1               - spawn heartbeat type 1\n");
		terminal_write("  yield                   - yield scheduler\n");
		terminal_write("  sync                    - save filesystem to disk\n");
		terminal_write("  exit                    - save and shut down\n");
		terminal_write("  shop                    - list files/directories here\n");
		terminal_write("  fab <file>              - create file\n");
		terminal_write("  insp <file>             - read file contents\n");
		terminal_write("  carve <text> :: <file>  - write text to file\n");
		terminal_write("  burn <file>             - delete file\n");
		terminal_write("  newdir <dir>            - create directory\n");
		terminal_write("  cd <dir>                - change directory\n");
		terminal_write("  learn <spell.ms>        - mark script as learned\n");
		terminal_write("  cast <spell.ms>         - execute learned script\n");
		terminal_write("  grimoire                - list learned spells\n");
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
	} else if (streq(buf, "grimoire")) {
		terminal_write("Learned spells:\n");
		vfs_grimoire(grimoire_print_cb, 0);
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
				size_t out_len = 0;

				for (size_t i = 0; i < text_len; i++) {
					if (rest[i] == '\\' && (i + 1) < text_len) {
						char n = rest[i + 1];

						if (n == 'n') {
							text[out_len++] = '\n';
							i++;
							continue;
						} else if (n == 't') {
							text[out_len++] = '\t';
							i++;
						    	continue;
						} else if (n == '\\') {
							text[out_len++] = '\\';
							i++;
							continue;
						}
					}
					text[out_len++] = rest[i];
				}
				text[out_len] = '\0';

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
      	} else if (starts_with(buf, "learn ")) {
		vfs_status_t st = vfs_learn(buf + 6);
		if (st == VFS_OK) terminal_write("Spell learned.\n");
		else vfs_print_status(st);
		if (vfs_is_dirty()) vfs_save();
	} else if (starts_with(buf, "cast ")) {
		shell_cast_script(buf + 5, depth);
      	} else {
      		terminal_write("Unknown command. Use 'help' for commands.\n");
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

		history_push(buf);
		shell_execute_command(buf, 0, 0);

		kfree(buf);
		yield();
	}
}

