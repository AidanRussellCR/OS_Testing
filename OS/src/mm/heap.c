#include <stdint.h>
#include <stddef.h>

extern uint8_t end;

static uintptr_t heap_ptr;

void heap_init(void) {
	heap_ptr = (uintptr_t)&end;
	heap_ptr = (heap_ptr + 15) & ~((uintptr_t)15);
}

void* kmalloc(size_t n) {
	if (n == 0) return 0;
	heap_ptr = (heap_ptr + 15) & ~((uintptr_t)15);
	void* p = (void*)heap_ptr;
	heap_ptr += n;
	return p;
}
