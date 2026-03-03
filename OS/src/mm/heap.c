#include <stddef.h>
#include <stdint.h>
#include "mm/heap.h"

extern uint8_t end;

typedef struct heap_block {
	size_t size;
	int free;
	struct heap_block* next;
	struct heap_block* prev;
} heap_block_t;

static heap_block_t* g_head = 0;
static uintptr_t g_brk = 0;

static inline uintptr_t align16(uintptr_t x) {
	return (x + 15u) & ~((uintptr_t)15u);
}

static heap_block_t* request_block(size_t size) {
	size = (size_t)align16((uintptr_t)size);

	uintptr_t base = align16(g_brk);
	heap_block_t* blk = (heap_block_t*)base;
	blk->size = size;
	blk->free = 0;
	blk->next = 0;
	blk->prev = 0;

	g_brk = base + sizeof(heap_block_t) + size;
	return blk;
}

static void split_block(heap_block_t* blk, size_t want) {
	want = (size_t)align16((uintptr_t)want);
	if (blk->size < want + sizeof(heap_block_t) + 16) return;

	uintptr_t blk_base = (uintptr_t)blk;
	uintptr_t new_base = blk_base + sizeof(heap_block_t) + want;
	new_base = align16(new_base);

	heap_block_t* newblk = (heap_block_t*)new_base;
	newblk->size = (blk->size + (blk_base + sizeof(heap_block_t)) - (new_base + sizeof(heap_block_t)));
	newblk->free = 1;

	newblk->next = blk->next;
	newblk->prev = blk;
	if (newblk->next) newblk->next->prev = newblk;
	blk->next = newblk;

	blk->size = want;
}

static void coalesce(heap_block_t* blk) {
	// merge forward
	if (blk->next && blk->next->free) {
		heap_block_t* n = blk->next;
		blk->size += sizeof(heap_block_t) + n->size;
		blk->next = n->next;
		if (blk->next) blk->next->prev = blk;
	}
	// merge backward
	if (blk->prev && blk->prev->free) {
		heap_block_t* p = blk->prev;
		p->size += sizeof(heap_block_t) + blk->size;
		p->next = blk->next;
		if (p->next) p->next->prev = p;
	}
}

void heap_init(void) {
	uintptr_t start = align16((uintptr_t)&end);
	g_head = 0;
	g_brk = start;
}

void* kmalloc(size_t bytes) {
	if (bytes == 0) return 0;
	bytes = (size_t)align16((uintptr_t)bytes);

	for (heap_block_t* b = g_head; b; b = b->next) {
		if (b->free && b->size >= bytes) {
			b->free = 0;
			split_block(b, bytes);
			return (void*)((uintptr_t)b + sizeof(heap_block_t));
		}
	}

	heap_block_t* blk = request_block(bytes);
	if (!g_head) {
		g_head = blk;
	} else {
		heap_block_t* tail = g_head;
		while (tail->next) tail = tail->next;
		tail->next = blk;
		blk->prev = tail;
	}
	return (void*)((uintptr_t)blk + sizeof(heap_block_t));
}

void kfree(void* ptr) {
	if (!ptr) return;
	heap_block_t* blk = (heap_block_t*)((uintptr_t)ptr - sizeof(heap_block_t));
	blk->free = 1;
	coalesce(blk);
}

