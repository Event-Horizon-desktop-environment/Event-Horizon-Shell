#pragma once

#include "configuration/shell_config.hpp"
#include "m3/core/primitives/box.hpp"

#include "desktop_shell/shared/core/cairo_helpers.hpp"

#include <cairo/cairo.h>

namespace eh::shell::shared {

// Paint a system-monitor-style glass card: dark frosted base (dockFill scaled
// by bgMul) with the shared m3::Box glass treatment (top sheen + bright inner
// rim + faint outer hairline + offset shadow).
inline void paint_glass_card(cairo_t* cr, double x, double y, double w, double h,
                             double radius, const eh::config::ChromePaintColors& mc,
                             double alphaMul = 1.0, double bgMul = 0.35,
                             double borderA = 0.12) {
  if (w <= 0 || h <= 0) return;
  (void)borderA; // Edges come from the shared Box treatment, not a flat border.

  const double bgR = mc.dockFillR * bgMul;
  const double bgG = mc.dockFillG * bgMul;
  const double bgB = mc.dockFillB * bgMul;
  const double bgA = 0.78 * std::clamp(alphaMul, 0.0, 1.0);

  m3::Box box;
  box.setColor(static_cast<float>(bgR), static_cast<float>(bgG), static_cast<float>(bgB),
               static_cast<float>(bgA));
  box.setRadius(static_cast<float>(radius));
  box.setGeometry(static_cast<float>(x), static_cast<float>(y),
                  static_cast<float>(w), static_cast<float>(h));
  box.setGlassy(true);
  box.paint(cr);
}

} // namespace eh::shell::shared
