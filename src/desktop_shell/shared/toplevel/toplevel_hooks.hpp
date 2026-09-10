#pragma once

struct DockApp;

[[nodiscard]] bool dock_foreign_toplevel_debug_enabled();

void dock_foreign_toplevel_bind_manager_and_hooks(DockApp& app);

void dock_ext_toplevels_set_changed_cb(DockApp& app);
