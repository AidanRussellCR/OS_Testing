#include <stddef.h>
#include <stdint.h>
#include "arsc/i386/ports.h"
#include "drivers/vga.h"

#define VGA_MEMORY ((uint16_t*)0xB8000)

static size_t term_row = 0;
static size_t term_col = 0;
static uint8_t term_color = 0x0F;

static inline uint16_t vga_entry(char c, uint8_t color) {
	return (uint16_t)c | ((uint16_t)color << 8);
}

size_t terminal_get_row(void) { return term_row; }
size_t terminal_get_col(void) { return term_col; }

void terminal_set_cursor_pos(size_t row, size_t col) {
	// avoid placing cursor in overlay
	if (row >= TERM_HEIGHT) row = TERM_HEIGHT - 1;
	if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;

	term_row = row;
	term_col = col;
	vga_cursor_set_pos(term_row, term_col);
}

void terminal_clear(void) {
	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			VGA_MEMORY[y * VGA_WIDTH + x] = vga_entry(' ', term_color);
		}
	}
	term_row = 0;
	term_col = 0;
}

void terminal_clear_row(size_t row) {
	for (size_t x = 0; x < VGA_WIDTH; x++) {
		VGA_MEMORY[row * VGA_WIDTH + x] = vga_entry(' ', term_color);
	}
}

void terminal_clear_text_area(void) {
	for (size_t y = 0; y < TERM_HEIGHT; y++) {
		terminal_clear_row(y);
	}
	term_row = 0;
	term_col = 0;
}

static void terminal_scroll_up(void) {
	for (size_t y = 1; y < TERM_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
		}
	}
	terminal_clear_row(TERM_HEIGHT - 1);
}

static void terminal_newline(void) {
	while (term_col < VGA_WIDTH) {
		VGA_MEMORY[term_row * VGA_WIDTH + term_col] = vga_entry(' ', term_color);
		term_col++;
	}
	term_col = 0;

	if (term_row + 1 >= TERM_HEIGHT) {
		terminal_scroll_up();
		term_row = TERM_HEIGHT - 1;
	} else {
		term_row++;
	}
	vga_cursor_set_pos(term_row, term_col);
}

void terminal_putc(char c) {
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

void terminal_putc_at(size_t row, size_t col, char c) {
	if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
	VGA_MEMORY[row * VGA_WIDTH + col] = vga_entry(c, term_color);
}

void terminal_write(const char* s) {
	for (size_t i = 0; s[i] != '\0'; i++) {
		terminal_putc(s[i]);
	}
}

void terminal_write_at(size_t row, size_t col, const char* s) {
	for (size_t i = 0; s[i] != '\0' && (col + i) < VGA_WIDTH; i++) {
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

