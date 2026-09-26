#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/desktop/widgets/calendar/calendar_card_paint.hpp"

#include <algorithm>
#include <cmath>

#include <cairo/cairo.h>

namespace eh::shell::dock::popup::calendar {
namespace {

constexpr double kShadowOffX = 2.0, kShadowOffY = 3.0, kShadowAlpha = 0.22;

void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  const double rad = std::min({r, w * 0.5, h * 0.5});
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - rad, y + rad,     rad, -M_PI_2, 0);
  cairo_arc(cr, x + w - rad, y + h - rad, rad,       0, M_PI_2);
  cairo_arc(cr, x + rad,     y + h - rad, rad,  M_PI_2,   M_PI);
  cairo_arc(cr, x + rad,     y + rad,     rad,      M_PI, 3.0 * M_PI_2);
  cairo_close_path(cr);
}

}

template<typename A>
inline void dock_calendar_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  // Identical pixels to the desktop calendar widget: same shared card paint,
  // same hit-test geometry (calendar_card_paint.hpp).
  const double W = static_cast<double>(kCalendarPopupW);
  const double H = static_cast<double>(kCalendarPopupH);
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);

  cairo_save(cr);
  rounded_rect(cr, kShadowOffX, kShadowOffY, W, H, 24.0);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, kShadowAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  eh::shell::desktop::paint_calendar_card(cr, W, H, 1.0, mc,
                                           sc.appearance.overlayOpacityWidgetCard,
                                           app.calDisplayDate, app.calSelectedDate);
}

}
