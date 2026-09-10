#pragma once

#include <cstdint>

typedef struct _cairo_surface cairo_surface_t;
typedef struct _cairo cairo_t;

struct DockApp;
namespace eh::config { struct ShellConfig; }

namespace eh::shell::dock::popup::weather {

constexpr int kWeatherPopupW = 360;
constexpr int kWeatherPopupH = 360;

template<typename A>
void dock_weather_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc);

void dock_weather_popup_handle_click(DockApp& app, double x, double y, uint32_t serial);

}

namespace eh::widgets::popup::weather {
using eh::shell::dock::popup::weather::kWeatherPopupW;
using eh::shell::dock::popup::weather::kWeatherPopupH;
template<typename A>
inline void weather_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  eh::shell::dock::popup::weather::dock_weather_popup_paint(app, cr, sc);
}
inline void weather_popup_handle_click(DockApp& app, double x, double y, uint32_t serial) {
  eh::shell::dock::popup::weather::dock_weather_popup_handle_click(app, x, y, serial);
}
template<typename A>
inline void weather_popup_handle_click(A& app, double x, double y, uint32_t serial) {
  eh::shell::dock::popup::weather::dock_weather_popup_handle_click(app, x, y, serial);
}
}

#include "desktop_shell/widgets/popup/weather/weather_popup.tpp"
