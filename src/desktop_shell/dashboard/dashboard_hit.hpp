#pragma once

#include "desktop_shell/dashboard/dashboard_types.hpp"

namespace eh::shell::dashboard {

// Resolves a surface-local pointer position against the rects stored by the
// last paint. Sub-rects win over the card body; subs are tested in paint
// order so earlier (more specific) affordances win an overlap.
[[nodiscard]] DashboardHit dashboard_hit_at(const DockApp& app, double px, double py);

}  // namespace eh::shell::dashboard
