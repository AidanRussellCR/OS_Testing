#pragma once
#include <stdint.h>

int streq(const char* a, const char* b);
int starts_with(const char* s, const char* prefix);
int parse_u32(const char* s, uint32_t* out);
