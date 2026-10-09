#pragma once

#include "configuration/shell_config.hpp"

#include <algorithm>
#include <cmath>

#include <cairo/cairo.h>

namespace eh::shell::dock::popup::volume_mixer {

// Single paint implementation lives in the .cpp (detail::mixer_paint); these
// thin adapters just build the view from any shell App type (Dock / Panel /
// Taskbar all expose popupW/popupH/pointerX/pointerY/pointerSurface/
// popupSurface/icons).
template<typename A>
inline void dock_volume_mixer_popup_paint(const A& app, cairo_t* cr,
                                          const eh::config::ShellConfig& sc) {
  detail::MixerView v;
  v.popupW = app.popupW;
  v.popupH = app.popupH;
  v.px = app.pointerX;
  v.py = app.pointerY;
  v.onPopup = (app.pointerSurface == app.popupSurface);
  v.icons = &const_cast<A&>(app).icons;
  detail::mixer_paint(cr, sc, v);
}

template<typename A>
inline void dock_volume_mixer_popup_handle_click_generic(A& app, double x, double y) {
  (void)app;
  detail::mixer_click_at(x, y, static_cast<double>(app.popupH));
}

template<typename A>
inline void dock_volume_mixer_popup_handle_motion_generic(A& app) {
  if (app.pointerSurface != app.popupSurface) return;
  detail::mixer_motion_at(app.pointerX, app.pointerY);
}

template<typename A>
inline void dock_volume_mixer_popup_handle_release_generic(A& app) {
  detail::mixer_release_at(app.pointerX);
}

template<typename A>
inline bool dock_volume_mixer_popup_handle_axis_generic(A& app, double deltaPx) {
  if (app.pointerSurface != app.popupSurface) return false;
  return detail::mixer_axis_at(app.pointerX, app.pointerY, deltaPx);
}

}  // namespace eh::shell::dock::popup::volume_mixer
