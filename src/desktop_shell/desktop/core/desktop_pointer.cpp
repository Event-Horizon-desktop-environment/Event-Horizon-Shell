#include "desktop_shell/desktop/core/desktop_pointer.hpp"

#include "desktop_shell/desktop/core/desktop_layer.hpp"
#include "desktop_shell/desktop/core/desktop_app.hpp"

#include "desktop_shell/dock/output/dock_layer_outputs.hpp"

#include <wayland-client.h>

namespace eh::shell::desktop {

namespace {

const eh::wayland::LogicalOutputBounds* output_bounds_for(const DesktopApp& app, const wl_output* out) {
  if (!out || !app.wl) return nullptr;
  const auto& bounds = app.wl->logical_output_bounds();
  for (const auto& b : bounds)
    if (b.output == out) return &b;
  return nullptr;
}

}

bool desktop_pointer_local_xy(DesktopApp& app, size_t layer_idx, double* lx, double* ly) {
   
  if (layer_idx >= app.layers.size() || !app.layers[layer_idx] || !lx || !ly) return false;
  DesktopLayer& want = *app.layers[layer_idx];
  DesktopLayer* under = layer_from_surface(app, app.pointerSurface);
  if (!under || !want.wlOut) return false;
  if (under == &want) {
    *lx = app.pointerX;
    *ly = app.pointerY;
    return true;
  }
  const auto* sUnder = output_bounds_for(app, under->wlOut);
  const auto* sWant = output_bounds_for(app, want.wlOut);
  if (!sUnder || !sWant) return false;
  const double gx = static_cast<double>(sUnder->global_x) + app.pointerX;
  const double gy = static_cast<double>(sUnder->global_y) + app.pointerY;
  *lx = gx - static_cast<double>(sWant->global_x);
  *ly = gy - static_cast<double>(sWant->global_y);
  return true;
}

}
