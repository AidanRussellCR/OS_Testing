#pragma once
#include <stdint.h>

int streq(const char* a, const char* b);
int starts_with(const char* s, const char* prefix);
int parse_u32(const char* s, uint32_t* out);

size_t kstrlen(const char* s);
void kstrcpy(char* dst, const char* src);
int kstrncmp(const char* a, const char* b, size_t n);
void* kmemset(void* dst, int v, size_t n);
void* kmemcpy(void* dst, const void* src, size_t n);
void* kmemmove(void* dst, const void* src, size_t n);
const char* kstrstr(const char* haystack, const char* needle);
size_t kstrnlen(const char* s, size_t max);
void kstrncpy0(char* dst, const char* src, size_t dst_cap);

