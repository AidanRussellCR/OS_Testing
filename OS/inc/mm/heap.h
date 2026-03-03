#pragma once
#include <stddef.h>
#include <stdint.h>

void heap_init(void);
void* kmalloc(size_t bytes);
void kfree(void* ptr);
