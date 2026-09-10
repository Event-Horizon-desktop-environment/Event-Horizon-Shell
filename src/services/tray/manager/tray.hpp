#pragma once

struct DockApp;

void dock_tray_init(DockApp& app);
void dock_tray_shutdown(DockApp& app);
void dock_tray_sync_items(DockApp& app);
