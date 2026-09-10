#pragma once

#include "desktop_shell/dock/core/dock_app.h"

#include "desktop_shell/shared/layout/strip_geometry.hpp"

namespace eh::shell::dock {

using eh::shell::shared::kLrSectionGapPx;
using eh::shell::shared::LrStripLayout;
using eh::shell::shared::lr_strip_layout;
using eh::shell::shared::widget_strip_h_scale;
using eh::shell::shared::strip_surface_x_to_layout;
using eh::shell::shared::strip_layout_x_to_surface;

inline void dock_pill_geometry_dims(const DockApp& app, int cfgW, int cfgH, double& x, double& y, double& boxW,
                                     double& boxH) {
  const double w = static_cast<double>(cfgW);
  const double h = static_cast<double>(cfgH);
  const double sc = dock_ui_scale(app.settings);
  boxH = static_cast<double>(app.dockHeight);
  const double side = 24.0 * sc;
  boxW = std::max(120.0 * sc, w - side);
  x = (w - boxW) / 2.0;
  y = (h - boxH) + app.animOffsetPx;
}

inline void dock_active_canvas_dims(const DockApp& app, int& cw, int& ch) {
  if (!app.dockLayers.empty() && app.pointerDockLayerIdx < app.dockLayers.size() && app.dockLayers[app.pointerDockLayerIdx]) {
    const DockOutputLayer& L = *app.dockLayers[app.pointerDockLayerIdx];
    cw = L.configuredWidth;
    ch = L.configuredHeight;
    return;
  }
  cw = app.configuredWidth;
  ch = app.configuredHeight;
}

inline void dock_pill_geometry(const DockApp& app, double& x, double& y, double& boxW, double& boxH) {
  int cw{}, ch{};
  dock_active_canvas_dims(app, cw, ch);
  dock_pill_geometry_dims(app, cw, ch, x, y, boxW, boxH);
}

}
