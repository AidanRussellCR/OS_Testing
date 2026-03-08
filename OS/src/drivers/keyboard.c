#include <stdint.h>
#include "arsc/i386/ports.h"
#include "drivers/keyboard.h"

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

int keyboard_try_get_key(key_event_t* ev) {
	static int e0 = 0;

	if ((inb(0x64) & 0x01) == 0) return 0;
	uint8_t sc = inb(0x60);

	if (sc == 0xE0) { e0 = 1; return 0; }

	int released = (sc & 0x80) != 0;
	uint8_t code = sc & 0x7F;

	if (!e0 && (code == 0x2A || code == 0x36)) {
		shift_down = released ? 0 : 1;
		return 0;
	}

	if (released) { e0 = 0; return 0; }

	if (e0) {
		e0 = 0;
		switch (code) {
			case 0x48: ev->type = KEY_UP;		return 1;
			case 0x50: ev->type = KEY_DOWN; 	return 1;
			case 0x49: ev->type = KEY_PAGEUP; 	return 1;
			case 0x51: ev->type = KEY_PAGEDOWN; 	return 1;
			case 0x4B: ev->type = KEY_LEFT; 	return 1;
			case 0x4D: ev->type = KEY_RIGHT; 	return 1;
			case 0x53: ev->type = KEY_DELETE; 	return 1;
			default: return 0;
		}
	}

	char c = shift_down ? scancode_to_ascii_shift[code] : scancode_to_ascii[code];
	if (!c) return 0;

	if (c == '\n') { ev->type = KEY_ENTER; return 1; }
	if (c == '\b') { ev->type = KEY_BACKSPACE; return 1; }

	if ((unsigned char)c < 32 || (unsigned char)c > 126) return 0;

	ev->type = KEY_CHAR;
	ev->ch = c;
	return 1;
}

