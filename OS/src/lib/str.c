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

size_t kstrlen(const char* s) {
	size_t n = 0;
	while (s && s[n]) n++;
	return n;
}

void kstrcpy(char* dst, const char* src) {
	if (!dst) return;
	if (!src) { dst[0] = '\0'; return; }
	size_t i = 0;
	while (src[i]) { dst[i] = src[i]; i++; }
	dst[i] = '\0';
}

int kstrncmp(const char* a, const char* b, size_t n) {
	for (size_t i = 0; i < n; i++) {
		unsigned char ac = a ? (unsigned char)a[i] : 0;
		unsigned char bc = b ? (unsigned char)b[i] : 0;
		if (ac != bc) return (ac < bc) ? -1 : 1;
		if (ac == 0) return 0;
	}
	return 0;
}

void* kmemset(void* dst, int v, size_t n) {
	unsigned char* p = (unsigned char*)dst;
	for (size_t i = 0; i < n; i++) p[i] = (unsigned char)v;
	return dst;
}

void* kmemcpy(void* dst, const void* src, size_t n) {
	unsigned char* d = (unsigned char*)dst;
	const unsigned char* s2 = (const unsigned char*)src;
	for (size_t i = 0; i < n; i++) d[i] = s2[i];
	return dst;
}

void* kmemmove(void* dst, const void* src, size_t n) {
	unsigned char* d = (unsigned char*)dst;
	const unsigned char* s2 = (const unsigned char*)src;
	if (d == s2 || n == 0) return dst;
	if (d < s2) {
		for (size_t i = 0; i < n; i++) d[i] = s2[i];
	} else {
		for (size_t i = n; i > 0; i--) d[i - 1] = s2[i - 1];
	}
	return dst;
}

const char* kstrstr(const char* haystack, const char* needle) {
	if (!haystack || !needle) return 0;
	if (needle[0] == '\0') return haystack;
	for (size_t i = 0; haystack[i]; i++) {
		size_t j = 0;
		while (needle[j] && haystack[i + j] && haystack[i + j] == needle[j]) j++;
		if (needle[j] == '\0') return &haystack[i];
	}
	return 0;
}

size_t kstrnlen(const char* s, size_t max) {
	size_t i = 0;
	if (!s) return 0;
	while (i < max && s[i]) i++;
	return i;
}

void kstrncpy0(char* dst, const char* src, size_t dst_cap) {
	if (!dst || dst_cap == 0) return;
	size_t i = 0;
	if (src) {
		for (; i + 1 < dst_cap && src[i]; i++) dst[i] = src[i];
	}
	dst[i] = '\0';
}

