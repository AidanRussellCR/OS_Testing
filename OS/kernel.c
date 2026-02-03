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

static void terminal_backspace(void) {
	if (term_col == 0) {
		if (term_row == 0) return;
		term_row--;
		term_col = VGA_WIDTH - 1;
	} else {
		term_col--;
	}
	VGA_MEMORY[term_row * VGA_WIDTH + term_col] = vga_entry(' ', term_color);
}

// I/O

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

// Keyboard scanning

static const char scancode_to_ascii[128] = {
	0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
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

static uint8_t keyboard_read_scancode_blocking(void) {
	while ((inb(0x64) & 0x01) == 0) { }
	return inb(0x60);
}

static char keyboard_get_char_blocking(void) {
	for (;;) {
		uint8_t sc = keyboard_read_scancode_blocking();

		int released = (sc & 0x80) != 0;
		uint8_t code = sc & 0x7F;

		if (code == 0x2A || code == 0x36) {
			shift_down = released ? 0 : 1;
			continue;
		}

		if (released) continue;

		char c = shift_down ? scancode_to_ascii_shift[code] : scancode_to_ascii[code];
		if (c == 0) continue; //unmapped key
		return c;
	}
}

// Shutdown

__attribute__((noreturn))
static void shutdown_machine(void) {
	outw(0x604, 0x2000);
	outw(0xB004, 0x2000);
	outw(0x4004, 0x3400);


	for (;;) {
		__asm__ volatile ("cli; hlt");
	}
}

// Input Handling

#define INPUT_MAX 128

static void prompt(void) {
	terminal_write("> ");
}

static void read_line(char* out, size_t out_cap) {
	size_t len = 0;

	for (;;) {
		char c = keyboard_get_char_blocking();

		// Enter
		if (c == '\n') {
			terminal_putc('\n');
			out[len] = '\0';
			return;
		}
		// Backspace
		if (c == '\b') {
			if (len > 0) {
				len--;
				terminal_backspace();
			}
			continue;
		}

		if ((unsigned char)c < 32 || (unsigned char)c > 126) {
			continue;
		}

		if (len + 1 < out_cap) {
			out[len++] = c;
			terminal_putc(c); // echo
		}
	}
}

// Kernel Main

void kmain(void) {
	terminal_clear();
	terminal_write("Hello World!\n");
	terminal_write("This is a working kernel terminal with text output.\n");

	char buf[INPUT_MAX];

	for (;;) {
		prompt();
		read_line(buf, sizeof(buf));

		if (streq(buf, "thanks")) {
			terminal_write("You're welcome!\n");
		} else if (streq(buf, "exit")) {
			terminal_write("Shutting down...\n");
			shutdown_machine();
		} else {
			terminal_write(buf);
			terminal_putc('\n');
		}
	}
}
