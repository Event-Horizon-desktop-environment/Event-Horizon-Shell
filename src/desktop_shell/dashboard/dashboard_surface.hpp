#pragma once

#include "configuration/shell_config.hpp"

namespace eh::shell::stage { struct StageApp; }

namespace eh::shell::dashboard {

// Permanent trigger strip while enabled (static strip region from birth).
void dashboard_ensure_trigger(eh::shell::stage::StageApp& app);
void dashboard_destroy_trigger(eh::shell::stage::StageApp& app);
// Panel surface for the open state (static full-surface region from birth,
// destroyed on close so no input remains).
void dashboard_ensure_panel(eh::shell::stage::StageApp& app);
void dashboard_destroy_panel(eh::shell::stage::StageApp& app);
void dashboard_shutdown(eh::shell::stage::StageApp& app);

void dashboard_draw(eh::shell::stage::StageApp& app);
void dashboard_frame_draw(eh::shell::stage::StageApp& app);
void dashboard_timer_tick(eh::shell::stage::StageApp& app);
void dashboard_on_config_changed(eh::shell::stage::StageApp& app, const eh::config::DashboardConfig& next);
void dashboard_weather_redraw(eh::shell::stage::StageApp& app);

// Requests the height the current layout needs. Returns true when a set_size
// was issued — the following configure event performs the repaint.
[[nodiscard]] bool dashboard_refresh_size(eh::shell::stage::StageApp& app);

// Layout-affecting state changed: resize if needed, otherwise repaint now.
void dashboard_changed(eh::shell::stage::StageApp& app);

}  // namespace eh::shell::dashboard
