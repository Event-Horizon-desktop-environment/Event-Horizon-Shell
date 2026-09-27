#pragma once

#include "desktop_shell/dashboard/dashboard_types.hpp"

#include <cairo/cairo.h>

namespace eh::shell::stage { struct StageApp; }

namespace eh::shell::dashboard {

// Clears the buffer, computes + stores the layout, then paints the panel
// translated by the reveal animation. Safe to call on every frame.
void dashboard_paint(eh::shell::stage::StageApp& app, cairo_t* cr, double surfaceW, double surfaceH);

}  // namespace eh::shell::dashboard
