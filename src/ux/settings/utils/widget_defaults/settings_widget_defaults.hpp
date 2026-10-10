#pragma once

// Canonical default widget layouts for the dock / taskbar / panel bars.
//
// Single source of truth shared by the config loader (shell_config.cpp
// apply_*_defaults_if_needed) and the settings widgets-tab Reset buttons, so
// the "reset to defaults" layout can never drift from what a fresh install
// gets.

#include <string>
#include <vector>

namespace eh::settings::widget_defaults {

inline std::vector<std::string> dock_left() { return {"smenu", "media"}; }
inline std::vector<std::string> dock_center() {
  return {"pinned_apps", "running_apps", "trash"};
}
inline std::vector<std::string> dock_right() {
  return {"tray", "weather", "clock", "pear_center", "settings_button"};
}

inline std::vector<std::string> taskbar_left() { return {"smenu", "win7_tasks"}; }
inline std::vector<std::string> taskbar_center() { return {}; }
inline std::vector<std::string> taskbar_right() {
  return {"tray", "weather", "clock", "pear_center", "settings_button"};
}

inline std::vector<std::string> panel_left() { return {"workspaces"}; }
inline std::vector<std::string> panel_center() { return {"clock"}; }
inline std::vector<std::string> panel_right() {
  return {"tray", "battery", "control_center", "notifications"};
}

}  // namespace eh::settings::widget_defaults
