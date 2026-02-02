#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((uint16_t*)0xB8000)

static size_t term_row = 0;
static size_t term_col = 0;
static uint8_t term_color = 0x0F;

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

static void terminal_putc(char c) {
	if (c == '\n') {
		term_col = 0;
		if (++term_row >= VGA_HEIGHT) term_row = 0;
		return;
	}

	VGA_MEMORY[term_row * VGA_WIDTH + term_col] = vga_entry(c, term_color);

	if (++term_col >= VGA_WIDTH) {
		term_col = 0;
		if (++term_row >= VGA_HEIGHT) term_row = 0;
	}
}

static void terminal_write(const char* s) {
	for (size_t i = 0; s[i] != '\0'; i++) {
		terminal_putc(s[i]);
	}
}

void kmain(void) {
	terminal_clear();
	terminal_write("Hello World!\n");
	terminal_write("This is a tiny kernel terminal.\n");

	for (;;) {
		__asm__ volatile ("hlt");
	}
}
