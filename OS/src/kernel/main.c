#include <stdint.h>
#include "drivers/vga.h"
#include "kernel/task.h"
#include "kernel/sched.h"
#include "kernel/shell.h"
#include "ui/overlays.h"
#include "mm/heap.h"
#include "fs/vfs.h"

void kmain(void) {
	terminal_init();
	terminal_write("Hello World!\n");
	terminal_write("Current kernel features:\n");
	terminal_write(" - Echo user input\n - Shut down system\n - Tasking/Scheduling\n - File System\n\n");

	vga_cursor_hide();
	vga_cursor_enable();
	vga_cursor_set_pos(terminal_get_row(), terminal_get_col());

	terminal_write("Kernel starting tasks...\n");

	heap_init();
	vfs_init();
	if (vfs_load() != VFS_OK) {
		vfs_save();
	}

	task_init();

	task_create(task_shell, "shell");
	task_create(task_heartbeat0, "heartbeat0");
	task_create(task_heartbeat1, "heartbeat1");

	__asm__ volatile("cli");
	schedule();

	for (;;) {
		__asm__ volatile ("hlt");
	}
}

