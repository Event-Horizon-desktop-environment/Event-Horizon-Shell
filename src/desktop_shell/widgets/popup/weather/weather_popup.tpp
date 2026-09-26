#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/desktop/widgets/weather_fancy/fancy_weather_card_paint.hpp"

#include <algorithm>
#include <cmath>

#include <cairo/cairo.h>

namespace eh::shell::dock::popup::weather {

template<typename A>
inline void dock_weather_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  // Height comes from the shared card, so the popup is exactly as tall as the
  // desktop weather-fancy widget it mirrors.
  const double W = static_cast<double>(app.popupW > 0 ? app.popupW : kWeatherPopupW);
  const std::string wid =
      app.weatherInstanceId.empty() ? std::string("weather") : app.weatherInstanceId;
  const double H = static_cast<double>(eh::shell::desktop::measure_fancy_weather_height(sc, wid));

  cairo_save(cr);
  const double rad = 26.0;
  const double ox = 2.0, oy = 4.0;
  cairo_new_sub_path(cr);
  cairo_arc(cr, ox + W - rad, oy + rad,     rad, -M_PI_2, 0);
  cairo_arc(cr, ox + W - rad, oy + H - rad, rad,       0, M_PI_2);
  cairo_arc(cr, ox + rad,     oy + H - rad, rad,  M_PI_2,   M_PI);
  cairo_arc(cr, ox + rad,     oy + rad,     rad,      M_PI, 3.0 * M_PI_2);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.28);
  cairo_fill(cr);
  cairo_restore(cr);

  eh::shell::desktop::paint_fancy_weather_card(cr, W, sc, wid);
}

}
