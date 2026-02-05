#include <stddef.h>
#include <stdint.h>

// Kernel tasks
typedef enum {
	TASK_DEAD = 0,
	TASK_READY,
	TASK_RUNNING,
	TASK_BLOCKED
} task_state_t;

typedef struct task {
	uint32_t esp; // Stack pointer for task
	task_state_t state;
	const char* name;
	void (*entry)(void);
} task_t;

#define MAX_TASKS 8

#define HB_COL 60
#define HB0_ROW_BASE 0
#define HB1_ROW_BASE 4
#define HB_MAX_LINES 4

static task_t tasks[MAX_TASKS];
static int task_count = 0;
static int current_task = -1;
static int hud_dirty = 1;

// Stack allocation (temp)
#define STACK_SIZE 4096
static uint8_t stacks[MAX_TASKS][STACK_SIZE] __attribute__((aligned(16))); //4KB stack per task (temp)

// Terminal definitions
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((uint16_t*)0xB8000)

#define TERM_HEIGHT (VGA_HEIGHT - 1)

#define INPUT_MAX 128

static size_t term_row = 0;
static size_t term_col = 0;
static uint8_t term_color = 0x0F;

// Cursor Movement
typedef enum {
	KEY_NONE = 0,
	KEY_CHAR,
	KEY_ENTER,
	KEY_BACKSPACE,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_DELETE
} key_type_t;

typedef struct {
	key_type_t type;
	char ch; // valid if type == KEY_CHAR
} key_event_t;

//Prototypes
static void prompt(void);
static void read_line(char* out, size_t out_cap);
static void yield(void);
static void task_shell(void);
static void terminal_write_at(size_t row, size_t col, const char* s);
static void terminal_clear_row(size_t row);
static void terminal_clear_text_area(void);
static void overlays_redraw(void);
static void debug_hud_draw(void);
static void task_heartbeat0(void);
static void task_heartbeat1(void);
static void terminal_putc_at(size_t row, size_t col, char c);
static void overlay_clear_line(size_t row);
static void debug_hud_mark_dirty(void);
static int hb_instance_index(const char* hb_name, int my_id);
static void vga_cursor_enable(void);
static void vga_cursor_set_pos(size_t row, size_t col);
static void vga_cursor_hide(void);


static inline uint16_t vga_entry(char c, uint8_t color) {
	return (uint16_t)c | ((uint16_t)color << 8);
}

static void terminal_clear(void) {
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', term_color);
		}
	}
	term_row = 0;
	term_col = 0;
}

static void terminal_clear_row(size_t row) {
	for (size_t x = 0; x < VGA_WIDTH; x++) {
		VGA_MEMORY[row * VGA_WIDTH + x] = vga_entry(' ', term_color);
	}
}

static void terminal_clear_text_area(void) {
	for (size_t y = 0; y < TERM_HEIGHT; y++) {
		terminal_clear_row(y);
	}
	term_row = 0;
	term_col = 0;
}

static void overlays_redraw(void) {
	for (size_t r = 0; r < (HB1_ROW_BASE + HB_MAX_LINES); r++) {
		overlay_clear_line(r);
	}
	debug_hud_mark_dirty();
	debug_hud_draw();
}


static void overlay_clear_line(size_t row) {
	for (size_t c = HB_COL; c < VGA_WIDTH; c++) {
		terminal_putc_at(row, c, ' ');
	}
}

static void terminal_scroll_up(void) {
	for (size_t y = 1; y < TERM_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
		}
	}
	// Clear last row
	terminal_clear_row(TERM_HEIGHT - 1);
}

static void terminal_newline(void) {
	// Clear line to avoid visual fragments
	while (term_col < VGA_WIDTH) {
		VGA_MEMORY[term_row * VGA_WIDTH + term_col] = vga_entry(' ', term_color);
		term_col++;
	}

	term_col = 0;

	// If at bottom, scroll instead of wrapping
	if (term_row + 1 >= TERM_HEIGHT) {
		terminal_scroll_up();
		term_row = TERM_HEIGHT - 1;
	} else {
		term_row++;
	}
	
	vga_cursor_set_pos(term_row, term_col);
}

static void terminal_putc(char c) {
	if (c == '\n') {
		terminal_newline();
		return;
	}

	VGA_MEMORY[term_row * VGA_WIDTH + term_col] = vga_entry(c, term_color);

	if (++term_col >= VGA_WIDTH) {
		terminal_newline();
	}
	
	vga_cursor_set_pos(term_row, term_col);
}

static void terminal_putc_at(size_t row, size_t col, char c) {
	if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
	VGA_MEMORY[row * VGA_WIDTH + col] = vga_entry(c, term_color);
}

static void terminal_write(const char* s) {
	for (size_t i = 0; s[i] != '\0'; i++) {
		terminal_putc(s[i]);
	}
}

static void terminal_write_at(size_t row, size_t col, const char* s) {
	for (size_t i = 0; s[i] != '\0' && (col + i) < VGA_WIDTH; i++) {
		terminal_putc_at(row, col + i, s[i]);
	}
}

// ---------------I/O--------------- //

static inline uint8_t inb(uint16_t port) {
	uint8_t ret;
	__asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
	__asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static int streq(const char* a, const char* b) {
	size_t i = 0;
	while (a[i] && b[i]) {
		if (a[i] != b[i]) return 0;
		i++;
	}
	return a[i] == '\0' && b[i] == '\0';
}

static int starts_with(const char* s, const char* prefix) {
	for (size_t i = 0; prefix[i]; i++) {
		if (s[i] != prefix[i]) return 0;
	}
	return 1;
}

static int parse_u32(const char* s, uint32_t* out) {
	uint32_t v = 0;
	int any = 0;
	for (size_t i = 0; s[i]; i++) {
		char c = s[i];
		if (c < '0' || c > '9') return 0;
		any = 1;
		v = v * 10u + (uint32_t)(c - '0');
	}
	if (!any) return 0;
	*out = v;
	return 1;
}

static void vga_cursor_enable(void) {
	// Standard cursor
	outb(0x3D4, 0x0A);
	outb(0x3D5, (inb(0x3D5) & 0xC0) | 0);	// start scanline
	outb(0x3D4, 0x0B);
	outb(0x3D5, (inb(0x3D5) & 0xE0) | 15);	// end scanline
}

static void vga_cursor_hide(void) {
	outb(0x3D4, 0x0A);
	outb(0x3D5, 0x20); // disable cursor
}

static void vga_cursor_set_pos(size_t row, size_t col) {
	if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;
	if (col >= VGA_WIDTH)  col = VGA_WIDTH - 1;

	uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
	outb(0x3D4, 0x0F);
	outb(0x3D5, (uint8_t)(pos & 0xFF));
	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

// ----------Keyboard scanning----------- //

static const char scancode_to_ascii[128] = {
	0,   27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
	'\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
	'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
	'z','x','c','v','b','n','m',',','.','/', 0,'*', 0,' ',
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char scancode_to_ascii_shift[128] = {
	0, 27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
	'\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
	'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|',
	'Z','X','C','V','B','N','M','<','>','?', 0,'*', 0,' ',
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static int shift_down = 0;

static int keyboard_try_get_key(key_event_t* ev) {
	static int e0 = 0;

	if ((inb(0x64) & 0x01) == 0) return 0;
	uint8_t sc = inb(0x60);

	if (sc == 0xE0) { e0 = 1; return 0; }

	int released = (sc & 0x80) != 0;
	uint8_t code = sc & 0x7F;

	// shift
	if (!e0 && (code == 0x2A || code == 0x36)) {
		shift_down = released ? 0 : 1;
		return 0;
	}

	if (released) { e0 = 0; return 0; }

	// extended keys
	if (e0) {
		e0 = 0;
		switch (code) {
			case 0x4B: ev->type = KEY_LEFT;  return 1;
			case 0x4D: ev->type = KEY_RIGHT; return 1;
			case 0x53: ev->type = KEY_DELETE; return 1;
			default: return 0;
		}
	}

	// normal keys
	char c = shift_down ? scancode_to_ascii_shift[code] : scancode_to_ascii[code];
	if (!c) return 0;

	if (c == '\n') { ev->type = KEY_ENTER; return 1; }
	if (c == '\b') { ev->type = KEY_BACKSPACE; return 1; }

	if ((unsigned char)c < 32 || (unsigned char)c > 126) return 0;

	ev->type = KEY_CHAR;
	ev->ch = c;
	return 1;
}

// ---------------Shutdown----------------- //

__attribute__((noreturn))
static void shutdown_machine(void) {
	outw(0x604, 0x2000);
	outw(0xB004, 0x2000);
	outw(0x4004, 0x3400);


	for (;;) {
		__asm__ volatile ("cli; hlt");
	}
}

// -------------Tasking/Scheduling-------------- //

extern void ctx_switch(uint32_t* old_esp, uint32_t new_esp);

static int task_alloc_slot(void) {
	for (int i = 0; i < MAX_TASKS; i++) {
		if (tasks[i].state == TASK_DEAD) return i;
	}
	return -1;
}

static int task_kill(int id) {
	if (id < 0 || id >= MAX_TASKS) return 0;
	if (tasks[id].state == TASK_DEAD) return 0;

	// Don't kill the currently running task
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

static void task_exit(void) {
	// Yield forever
	for (;;) yield();
}

static void task_trampoline(void) {
	// current_task is already set before switching into this one
	void (*fn)(void) = tasks[current_task].entry;
	fn();
	task_exit();
}

static int task_create(void (*entry)(void), const char* name) {
	int id = task_alloc_slot();
	if (id < 0) return -1;

	task_t* t = &tasks[id];

	t->entry = entry;
	t->state = TASK_READY;
	t->name = name;

	uint32_t sp = (uint32_t)&stacks[id][STACK_SIZE];

	// ctx_switch does: popad; popfd; ret
	// initial stack:
		// [edi][esi][ebp][esp(dummy)][ebx][edx][ecx][eax][eflags][ret=entry]

	// ret address
	sp -= 4; *(uint32_t*)sp = (uint32_t)task_trampoline;

	// eflags
	sp -= 4; *(uint32_t*)sp = 0x00000002;

	// pushad layout
		// edi is at top
	sp -= 4; *(uint32_t*)sp = 0; // eax
	sp -= 4; *(uint32_t*)sp = 0; // ecx
	sp -= 4; *(uint32_t*)sp = 0; // edx
	sp -= 4; *(uint32_t*)sp = 0; // ebx
	sp -= 4; *(uint32_t*)sp = 0; // esp dummy
	sp -= 4; *(uint32_t*)sp = 0; // ebp
	sp -= 4; *(uint32_t*)sp = 0; // esi
	sp -= 4; *(uint32_t*)sp = 0; // edi

	t->esp = sp;

	if (id + 1 > task_count) task_count = id + 1;

	debug_hud_mark_dirty();

	return id;
}

static char state_char(task_state_t s) {
	switch (s) {
		case TASK_READY:   return 'R';
		case TASK_RUNNING: return '*';
		case TASK_BLOCKED: return 'B';
		case TASK_DEAD:    return 'D';
		default:           return '?';
	}
}

static void print_tasks_to_console(void) {
	terminal_write("ID STATE NAME\n");
	for (int i = 0; i < MAX_TASKS; i++) {
		if (tasks[i].state == TASK_DEAD) continue;

		terminal_putc('0' + (i % 10));
		terminal_write("  ");
		terminal_putc(state_char(tasks[i].state));
		terminal_write("     ");
		terminal_write(tasks[i].name ? tasks[i].name : "?");
		terminal_putc('\n');
	}
}

static void debug_hud_mark_dirty(void) {
	hud_dirty = 1;
}

static void debug_hud_draw(void) {
	if (!hud_dirty) return;
	hud_dirty = 0;

	const size_t hud_w = 26;
	const size_t hud_h = 6; // HUD rows
	const size_t start_col = VGA_WIDTH - hud_w;
	const size_t start_row = VGA_HEIGHT - hud_h;

	// Clear HUD area
	for (size_t r = 0; r < hud_h; r++) {
		for (size_t c = 0; c < hud_w; c++) {
			terminal_putc_at(start_row + r, start_col + c, ' ');
		}
	}

	terminal_write_at(start_row + 0, start_col, "Tasks");

	// List up to 5 tasks
	size_t line = 1;
	for (int i = 0; i < MAX_TASKS && line < hud_h; i++) {
		if (tasks[i].state == TASK_DEAD) continue;

		terminal_putc_at(start_row + line, start_col + 0, '#');
		terminal_putc_at(start_row + line, start_col + 1, '0' + (i % 10));
		terminal_putc_at(start_row + line, start_col + 2, ' ');
		terminal_putc_at(start_row + line, start_col + 3, state_char(tasks[i].state));
		terminal_putc_at(start_row + line, start_col + 4, ' ');
		const char* nm = tasks[i].name ? tasks[i].name : "?";

		size_t col = start_col + 5;
		for (size_t k = 0; nm[k] && col < VGA_WIDTH; k++, col++) {
			terminal_putc_at(start_row + line, col, nm[k]);
		}

		line++;
	}
}

static void schedule(void) {
	int prev = current_task;

	// Mark prev running task as ready unless dead or blocked
	if (prev >= 0 && tasks[prev].state == TASK_RUNNING) {
		tasks[prev].state = TASK_READY;
	}

	// Find next ready task
	int next = -1;
	for (int step = 1; step <= MAX_TASKS; step++) {
		int idx = (prev + step) % MAX_TASKS;
		if (tasks[idx].state == TASK_READY) {
			next = idx;
			break;
		}
	}

	// If no ready tasks, keep running prev if it exists and not dead
	if (next == -1) {
		if (prev >= 0 && tasks[prev].state != TASK_DEAD) {
			tasks[prev].state = TASK_RUNNING;
			debug_hud_draw();
			return;
		}
		// Nothing runnable at all
		debug_hud_draw();
		return;
	}

	current_task = next;
	tasks[current_task].state = TASK_RUNNING;

	debug_hud_draw();

	if (prev == -1) {
		uint32_t dummy = 0;
		ctx_switch(&dummy, tasks[current_task].esp);
	} else if (prev != current_task) {
		ctx_switch(&tasks[prev].esp, tasks[current_task].esp);
	}
}

static void yield(void) {
	schedule();
}

// Task Entry

static void task_shell(void) {
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
			print_tasks_to_console();
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

		// Yield each loop so other tasks get time
		yield();
	}
}

static void task_delay(volatile uint32_t loops) {
	for (volatile uint32_t i = 0; i < loops; i++) {
		__asm__ volatile ("pause");
		// Yield occasionally to not starve other tasks
		if ((i & 0x3FFF) == 0) yield();
	}
}

static int hb_instance_index(const char* hb_name, int my_id) {
	int idx = 0;
	for (int i = 0; i < MAX_TASKS; i++) {
		if (tasks[i].state == TASK_DEAD) continue;
		if (!tasks[i].name) continue;

		// match exact name
		if (!streq(tasks[i].name, hb_name)) continue;

		if (i == my_id) return idx;
		idx++;
	}
	return -1;
}

static void task_heartbeat0(void) {
	uint32_t n = 0;
	for (;;) {
		int me = current_task;
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

static void task_heartbeat1(void) {
	uint32_t n = 0;
	for (;;) {
		int me = current_task;
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

// -------------Input Handling-------------- //

static void prompt(void) {
	terminal_write("> ");
	vga_cursor_set_pos(term_row, term_col);
}

static void read_line(char* out, size_t out_cap) {
	size_t len = 0;
	size_t cur = 0;

	// Editable area starts right after "> "
	size_t input_row = term_row;
	size_t input_col = term_col;

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

			term_row = input_row;
			term_col = input_col + len;

			terminal_putc('\n');
			return;
		}

		if (ev.type == KEY_LEFT) {
			if (cur > 0) cur--;
		} else if (ev.type == KEY_RIGHT) {
			if (cur < len) cur++;
		} else if (ev.type == KEY_BACKSPACE) {
			if (cur > 0) {
				// delete char before cursor
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
				// insert at cursor, shift right
				for (size_t i = len; i > cur; i--) out[i] = out[i - 1];
				out[cur] = ev.ch;
				cur++;
				len++;
				out[len] = '\0';
			}
		}

		// Redraw the line
			// write buffer
		for (size_t i = 0; i < len; i++) {
			terminal_putc_at(input_row, input_col + i, out[i]);
		}
			// clear leftover chars from previous longer line
		for (size_t i = len; i < out_cap - 1 && (input_col + i) < VGA_WIDTH; i++) {
			terminal_putc_at(input_row, input_col + i, ' ');
		}

		// Place hardware cursor
		vga_cursor_set_pos(input_row, input_col + cur);
	}
}

// --------------Kernel Main---------------- //

void kmain(void) {
	terminal_clear();
	terminal_write("Hello World!\n");
	terminal_write("Current kernel features:\n");
	terminal_write(" - Echo user input\n - Shut down system\n - Tasking/Scheduling\n\n");

	vga_cursor_hide();
	vga_cursor_enable();
	vga_cursor_set_pos(term_row, term_col);

	terminal_write("Kernel starting tasks...\n");
	// "yield" is keyword for manual switching

	for (int i = 0; i < MAX_TASKS; i++) {
		tasks[i].state = TASK_DEAD;
	}
	task_count = 0;
	current_task = -1;

	for (int i = 0; i < MAX_TASKS; i++) {
		tasks[i].state = TASK_DEAD;
		tasks[i].name = 0;
		tasks[i].entry = 0;
		tasks[i].esp = 0;
	}

	task_create(task_shell, "shell");
	task_create(task_heartbeat0, "heartbeat0");
	task_create(task_heartbeat1, "heartbeat1");

	__asm__ volatile("cli");

	schedule();

	for (;;) {
		__asm__ volatile ("hlt");
	}
}
