#pragma once

struct DockApp;

void dock_tooltip_start_timer(DockApp& app, int slotIdx);
void dock_tooltip_cancel(DockApp& app);
void dock_tooltip_tick(DockApp& app);
void dock_tooltip_cleanup(DockApp& app);
