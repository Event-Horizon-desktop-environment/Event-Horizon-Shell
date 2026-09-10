#pragma once

#include <cairo.h>

#include <algorithm>
#include <cmath>

namespace eh::shell::shared {

inline void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  const double rad = std::min({r, w * 0.5, h * 0.5});
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - rad, y + rad,     rad, -M_PI_2,      0);
  cairo_arc(cr, x + w - rad, y + h - rad, rad,       0, M_PI_2);
  cairo_arc(cr, x + rad,     y + h - rad, rad,  M_PI_2,   M_PI);
  cairo_arc(cr, x + rad,     y + rad,     rad,     M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

inline void path_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double radius) {
  const double r = std::min({radius, w * 0.5, h * 0.5});
  const double p = std::numbers::pi;
  cairo_new_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -p * 0.5, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, p * 0.5);
  cairo_arc(cr, x + r, y + h - r, r, p * 0.5, p);
  cairo_arc(cr, x + r, y + r, r, p, p * 1.5);
  cairo_close_path(cr);
}

} // namespace eh::shell::shared
