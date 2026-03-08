#pragma once
#include <stdint.h>

typedef enum {
	KEY_NONE = 0,
	KEY_CHAR,
	KEY_ENTER,
	KEY_BACKSPACE,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_UP,
	KEY_DOWN,
	KEY_PAGEUP,
	KEY_PAGEDOWN,
	KEY_DELETE
} key_type_t;

typedef struct {
	key_type_t type;
	char ch;
} key_event_t;

int keyboard_try_get_key(key_event_t* ev);
