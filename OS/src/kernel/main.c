#include <stdint.h>
#include "drivers/vga.h"
#include "kernel/task.h"
#include "kernel/sched.h"
#include "kernel/shell.h"
#include "ui/overlays.h"

void kmain(void) {
	terminal_init();
	terminal_write("Hello World!\n");
	terminal_write("Current kernel features:\n");
	terminal_write(" - Echo user input\n - Shut down system\n - Tasking/Scheduling\n\n");

	vga_cursor_hide();
	vga_cursor_enable();
	vga_cursor_set_pos(terminal_get_row(), terminal_get_col());

	terminal_write("Kernel starting tasks...\n");

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
