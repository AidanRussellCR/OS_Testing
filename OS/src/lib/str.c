#include <stddef.h>
#include <stdint.h>
#include "lib/str.h"

int streq(const char* a, const char* b) {
	size_t i = 0;
	while (a[i] && b[i]) {
		if (a[i] != b[i]) return 0;
		i++;
	}
	return a[i] == '\0' && b[i] == '\0';
}

int starts_with(const char* s, const char* prefix) {
	for (size_t i = 0; prefix[i]; i++) {
		if (s[i] != prefix[i]) return 0;
	}
	return 1;
}

int parse_u32(const char* s, uint32_t* out) {
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

