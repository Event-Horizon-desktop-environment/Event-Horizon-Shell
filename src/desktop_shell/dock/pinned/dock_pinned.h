#pragma once

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/pinned/dock_pin_identity.hpp"

#include <string>

void dock_drawer_pinned_toggle(DockApp& app, const std::string& appKey);
[[nodiscard]] bool dock_is_drawer_pinned(DockApp& app, const std::string& rawDesktopOrAppKey);
[[nodiscard]] bool dock_settings_drawer_pinned(const DockSettings& s, const std::string& appKey);

void dock_pinned_toggle(DockApp& app, const std::string& appKey);
void dock_pinned_toggle_silent(DockApp& app, const std::string& appKey);
[[nodiscard]] bool dock_is_app_pinned(DockApp& app, const std::string& rawDesktopOrAppKey);

// Settings-only variants for the split-out components (horizon-desktop): read
// the pinnedApps list from the current shell config snapshot and persist any
// change by writing the dock settings toml. The dock picks the change up via
// its settings inotify, so no DockApp instance is required.
[[nodiscard]] bool dock_settings_is_app_pinned(const std::string& rawDesktopOrAppKey);
void dock_settings_pinned_toggle(const std::string& appKey);

void dock_start_menu_pinned_toggle(DockApp& app, const std::string& appKey);
[[nodiscard]] bool dock_is_start_menu_pinned(DockApp& app, const std::string& rawDesktopOrAppKey);
void dock_pinned_pointer_motion(DockApp& app);

const std::vector<std::string>& dock_pinned_apps_source_for_layout(const DockApp& app);
void dock_pin_drag_rebuild_paint_order(DockApp& app);
