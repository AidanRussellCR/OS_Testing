#include <stddef.h>
#include <stdint.h>
#include "arsc/i386/ports.h"
#include "drivers/vga.h"

#define VGA_MEMORY ((uint16_t*)0xB8000)

static size_t g_cursor_row = 0; // row in scrollback buffer
static size_t g_cursor_col = 0;
static uint8_t term_color = 0x0F;

static uint16_t g_textbuf[SCROLLBACK_ROWS][TEXT_WIDTH];
static size_t g_head_row = 0; // row being written
static size_t g_view_top = 0; // row currently displayed
static int g_follow_tail = 1; // if 1, view tracks newest output

static inline uint16_t vga_entry(char c, uint8_t color) {
	return (uint16_t)c | ((uint16_t)color << 8);
}

static void clear_textbuf_row(size_t row) {
	if (row >= SCROLLBACK_ROWS) return;
	for (size_t c = 0; c < TEXT_WIDTH; c++) {
		g_textbuf[row][c] = vga_entry(' ', term_color);
	}
}

static void sync_hw_cursor(void) {
	size_t vr = 0;
	if (g_cursor_row >= g_view_top) vr = g_cursor_row - g_view_top;
	if (vr >= TERM_HEIGHT) vr = TERM_HEIGHT - 1;

	size_t vc = g_cursor_col;
	if (vc >= TEXT_WIDTH) vc = TEXT_WIDTH - 1;

	vga_cursor_set_pos(vr, vc);
}

static void advance_to_next_line(void) {
	g_cursor_col = 0;

	if (g_cursor_row + 1 < SCROLLBACK_ROWS) {
		g_cursor_row++;
		clear_textbuf_row(g_cursor_row);
	} else {
		// shift scrollback up by one row
		for (size_t r = 1; r < SCROLLBACK_ROWS; r++) {
			for (size_t c = 0; c < TEXT_WIDTH; c++) {
				g_textbuf[r - 1][c] = g_textbuf[r][c];
			}
		}
		clear_textbuf_row(SCROLLBACK_ROWS - 1);
		// cursor stays on last row
		g_cursor_row = SCROLLBACK_ROWS - 1;

		if (g_view_top > 0) g_view_top--;
	}

	if (g_follow_tail) {
		g_view_top = (g_cursor_row >= (TERM_HEIGHT - 1))
			? (g_cursor_row - (TERM_HEIGHT - 1))
			: 0;
	}
}

size_t terminal_get_row(void) {
	if (g_cursor_row < g_view_top) return 0;
	size_t vr = g_cursor_row - g_view_top;
	if (vr >= TERM_HEIGHT) vr = TERM_HEIGHT - 1;
	return vr;
}

size_t terminal_get_col(void) { return g_cursor_col; }

size_t terminal_get_buffer_row(void) { return g_cursor_row; }
size_t terminal_get_view_top(void) { return g_view_top; }

static void render_text_window(void) {
	for (size_t vr = 0; vr < TERM_HEIGHT; vr++) {
		size_t src = g_view_top + vr;
		for (size_t c = 0; c < TEXT_WIDTH; c++) {
			char ch = ' ';
			if (src < SCROLLBACK_ROWS) {
				ch = (char)(g_textbuf[src][c] & 0xFF);
			}
			VGA_MEMORY[vr * VGA_WIDTH + c] = vga_entry(ch, term_color);
		}
		// clear scroll bar col for this row
		VGA_MEMORY[vr * VGA_WIDTH + SCROLLBAR_COL] = vga_entry(' ', term_color);
	}

	// draw  scroll bar
	size_t max_top = (g_head_row >= TERM_HEIGHT) ? (g_head_row - TERM_HEIGHT + 1) : 0;
	size_t marker_row = 0;
	if (max_top > 0) marker_row = (g_view_top * TERM_HEIGHT) / (max_top + 1);
	if (marker_row >= TERM_HEIGHT) marker_row = TERM_HEIGHT - 1;
	VGA_MEMORY[marker_row * VGA_WIDTH + SCROLLBAR_COL] = vga_entry('|', term_color);
	sync_hw_cursor();
}

void terminal_set_cursor_pos(size_t row, size_t col) {
	if (row >= TERM_HEIGHT) row = TERM_HEIGHT - 1;
	if (col >= TEXT_WIDTH) col = TEXT_WIDTH - 1;

	g_cursor_row = g_view_top + row;
	if (g_cursor_row >= SCROLLBACK_ROWS) g_cursor_row = SCROLLBACK_ROWS - 1;
	g_cursor_col = col;

	sync_hw_cursor();
}

void terminal_ensure_row_visible(size_t row) {
	if (row >= SCROLLBACK_ROWS) row = SCROLLBACK_ROWS - 1;

	if (row < g_view_top) {
		g_view_top = row;
		render_text_window();
		sync_hw_cursor();
		return;
	}

	if (row >= g_view_top + TERM_HEIGHT) {
		g_view_top = row - (TERM_HEIGHT - 1);
		render_text_window();
		sync_hw_cursor();
		return;
	}
}

void terminal_scroll_view_up(void) {
	if (g_view_top > 0) g_view_top--;
	g_follow_tail = 0;
	render_text_window();
	sync_hw_cursor();
}

void terminal_scroll_view_down(void) {
	size_t max_top = (g_head_row >= TERM_HEIGHT) ? (g_head_row - TERM_HEIGHT + 1) : 0;
	if (g_view_top < max_top) g_view_top++;
	if (g_view_top == max_top) g_follow_tail = 1;
	render_text_window();
	sync_hw_cursor();
}

void terminal_follow_tail(void) {
	g_view_top = (g_head_row >= TERM_HEIGHT) ? (g_head_row - TERM_HEIGHT + 1) : 0;
	g_follow_tail = 1;
	render_text_window();
	sync_hw_cursor();
}

int terminal_is_following_tail(void) {
	return g_follow_tail;
}

void terminal_clear(void) {
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', term_color);
		}
	}

	for (size_t r = 0; r < SCROLLBACK_ROWS; r++) {
		clear_textbuf_row(r);
	}

	g_cursor_row = 0;
	g_cursor_col = 0;
	g_head_row = 0;
	g_view_top = 0;
	g_follow_tail = 1;

	render_text_window();
	sync_hw_cursor();
}

void terminal_clear_row(size_t row) {
	if (row < TERM_HEIGHT) {
		size_t abs = g_view_top + row;
		if (abs < SCROLLBACK_ROWS) {
			clear_textbuf_row(abs);
			render_text_window();
			sync_hw_cursor();
		}
	} else {
		// overlay rows draw direct to VGA
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			VGA_MEMORY[row * VGA_WIDTH + x] = vga_entry(' ', term_color);
		}
	}
}

void terminal_clear_text_area(void) {
	for (size_t r = 0; r < SCROLLBACK_ROWS; r++) {
		clear_textbuf_row(r);
	}

	g_cursor_row = 0;
	g_cursor_col = 0;
	g_head_row = 0;
	g_view_top = 0;
	g_follow_tail = 1;

	render_text_window();
	sync_hw_cursor();
}

static void terminal_newline(void) {
	while (g_cursor_col < TEXT_WIDTH) {
		g_textbuf[g_cursor_row][g_cursor_col] = vga_entry(' ', term_color);
		g_cursor_col++;
	}

	advance_to_next_line();
	g_head_row = g_cursor_row;

	render_text_window();
	sync_hw_cursor();
}

void terminal_putc(char c) {
	if (c == '\n') {
		terminal_newline();
		return;
	}

	if (g_cursor_row >= SCROLLBACK_ROWS) g_cursor_row = SCROLLBACK_ROWS - 1;
	if (g_cursor_col >= TEXT_WIDTH) {
		terminal_newline();
	}

	g_textbuf[g_cursor_row][g_cursor_col] = vga_entry(c, term_color);
	g_cursor_col++;

	if (g_cursor_col >= TEXT_WIDTH) {
		terminal_newline();
	} else {
		g_head_row = g_cursor_row;
		if (g_follow_tail) {
			g_view_top = (g_cursor_row >= (TERM_HEIGHT - 1))
				? (g_cursor_row - (TERM_HEIGHT - 1))
				: 0;
		}
		render_text_window();
		sync_hw_cursor();
	}
}

void terminal_putc_at(size_t row, size_t col, char c) {
	if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;

	if (row < TERM_HEIGHT) {
		if (col >= TEXT_WIDTH) return; // preserve scroll bar col
		size_t abs = g_view_top + row;
		if (abs >= SCROLLBACK_ROWS) return;

		g_textbuf[abs][col] = vga_entry(c, term_color);
		VGA_MEMORY[row * VGA_WIDTH + col] = vga_entry(c, term_color);
	} else {
		VGA_MEMORY[row * VGA_WIDTH + col] = vga_entry(c, term_color);
	}
}

void terminal_write(const char* s) {
	for (size_t i = 0; s[i] != '\0'; i++) {
		terminal_putc(s[i]);
	}
}

void terminal_write_at(size_t row, size_t col, const char* s) {
	size_t limit = (row < TERM_HEIGHT) ? TEXT_WIDTH : VGA_WIDTH;

	for (size_t i = 0; s[i] != '\0' && (col + i) < limit; i++) {
		terminal_putc_at(row, col + i, s[i]);
	}
}

void vga_cursor_enable(void) {
	outb(0x3D4, 0x0A);
	outb(0x3D5, (inb(0x3D5) & 0xC0) | 0);
	outb(0x3D4, 0x0B);
	outb(0x3D5, (inb(0x3D5) & 0xE0) | 15);
}

void vga_cursor_hide(void) {
	outb(0x3D4, 0x0A);
	outb(0x3D5, 0x20);
}

void vga_cursor_set_pos(size_t row, size_t col) {
	if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;
	if (col >= VGA_WIDTH)  col = VGA_WIDTH - 1;

	uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
	outb(0x3D4, 0x0F);
	outb(0x3D5, (uint8_t)(pos & 0xFF));
	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void terminal_init(void) {
	terminal_clear();
}

