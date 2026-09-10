#pragma once

#include "configuration/shell_config.hpp"
#include "m3/core/primitives/box.hpp"

#include "desktop_shell/shared/core/cairo_helpers.hpp"

#include <cairo/cairo.h>

namespace eh::shell::shared {

// Paint a media_compact-style glass card: dark frosted base (dockFill scaled
// by bgMul) filled through m3::Box's glassy path (bright inner rim + top and
// bottom highlight bands + faint outer separation) plus a subtle white border.
// This is the exact recipe used by the media_compact desktop widget.
inline void paint_glass_card(cairo_t* cr, double x, double y, double w, double h,
                             double radius, const eh::config::ChromePaintColors& mc,
                             double alphaMul = 1.0, double bgMul = 0.35,
                             double borderA = 0.12) {
  if (w <= 0 || h <= 0) return;

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

  rounded_rect(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0, radius);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, borderA);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
}

} // namespace eh::shell::shared
