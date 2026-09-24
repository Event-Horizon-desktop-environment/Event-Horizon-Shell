#pragma once

#include "configuration/shell_config.hpp"

struct DockApp;

namespace eh::shell::dashboard {

// Permanent trigger strip while enabled (static strip region from birth).
void dashboard_ensure_trigger(DockApp& app);
void dashboard_destroy_trigger(DockApp& app);
// Panel surface for the open state (static full-surface region from birth,
// destroyed on close so no input remains).
void dashboard_ensure_panel(DockApp& app);
void dashboard_destroy_panel(DockApp& app);
void dashboard_shutdown(DockApp& app);

void dashboard_draw(DockApp& app);
void dashboard_frame_draw(DockApp& app);
void dashboard_timer_tick(DockApp& app);
void dashboard_on_config_changed(DockApp& app, const eh::config::DashboardConfig& next);
void dashboard_weather_redraw(DockApp& app);

// Requests the height the current layout needs. Returns true when a set_size
// was issued — the following configure event performs the repaint.
[[nodiscard]] bool dashboard_refresh_size(DockApp& app);

// Layout-affecting state changed: resize if needed, otherwise repaint now.
void dashboard_changed(DockApp& app);

}  // namespace eh::shell::dashboard
