#pragma once
#include <stddef.h>
#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define TERM_HEIGHT (VGA_HEIGHT - 1)

void terminal_init(void);
void terminal_clear(void);
void terminal_clear_row(size_t row);
void terminal_clear_text_area(void);
void terminal_write(const char* s);
void terminal_write_at(size_t row, size_t col, const char* s);
void terminal_putc(char c);
void terminal_putc_at(size_t row, size_t col, char c);

size_t terminal_get_row(void);
size_t terminal_get_col(void);
void terminal_set_cursor_pos(size_t row, size_t col);

/* VGA hardware cursor */
void vga_cursor_enable(void);
void vga_cursor_hide(void);
void vga_cursor_set_pos(size_t row, size_t col);
