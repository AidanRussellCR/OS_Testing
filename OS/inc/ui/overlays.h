#pragma once
#include <stddef.h>

#define HB_COL 60
#define HB0_ROW_BASE 0
#define HB1_ROW_BASE 4
#define HB_MAX_LINES 4

void overlays_redraw(void);
void overlay_clear_line(size_t row);

void debug_hud_mark_dirty(void);
void debug_hud_draw(void);

/* heartbeat tasks */
void task_heartbeat0(void);
void task_heartbeat1(void);
