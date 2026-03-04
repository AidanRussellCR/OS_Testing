#pragma once
#include <stdint.h>

void overlays_redraw(void);

// Heartbeat management
void overlays_hb_tick(int hb_kind, int task_id, uint32_t counter);
void overlays_hb_remove(int task_id);

// Heartbeat tasks
void task_heartbeat0(void);
void task_heartbeat1(void);

