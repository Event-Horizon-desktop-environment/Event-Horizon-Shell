#pragma once

#include <string>

struct DockApp;
struct DockSettings;

[[nodiscard]] std::string dock_first_control_center_widget_id(const DockSettings& s);

// Widget id backing the currently open quick-settings popup: the stored
// opener id when set, else the first configured quick-settings widget.
// Keeps legacy Control Center and PearCenter routed to their own designs
// when both sit on the bar.
[[nodiscard]] std::string dock_active_control_center_widget_id(const DockApp& app);
