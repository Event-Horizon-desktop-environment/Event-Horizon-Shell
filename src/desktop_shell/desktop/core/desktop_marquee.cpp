#include "desktop_shell/desktop/core/desktop_marquee.hpp"

#include "desktop_shell/desktop/icons/desktop_icon_menu.hpp"
#include "desktop_shell/desktop/icons/desktop_icons.hpp"
#include "desktop_shell/desktop/icons/desktop_icon_menu.hpp"
#include "desktop_shell/desktop/icons/desktop_menu.hpp"
#include "desktop_shell/desktop/entries/desktop_open_with.hpp"
#include "desktop_shell/desktop/core/desktop_pointer.hpp"
#include "desktop_shell/desktop/icons/desktop_menu.hpp"
#include "desktop_shell/desktop/core/desktop_app.hpp"
#include "desktop_shell/desktop/widgets/world_clock/desktop_world_clock_settings.hpp"

#include "desktop_shell/dock/output/dock_layer_outputs.hpp"


#include <algorithm>
#include <iostream>
#include <wayland-client.h>

namespace eh::shell::desktop {


static const eh::wayland::LogicalOutputBounds* output_bounds_for(const DesktopApp& app, const wl_output* out) {
   
  if (!out || !app.wl) return nullptr;
  const auto& bounds = app.wl->logical_output_bounds();
  auto it = std::find_if(bounds.begin(), bounds.end(),
                         [out](const auto& b) { return b.output == out; });
  return it != bounds.end() ? &*it : nullptr;
}

bool marquee_drag_update_end_from_pointer(DesktopApp& app) {
   
  if (!app.marqueeDragging || app.marqueeLayerIdx >= app.layers.size()) return false;
  DesktopLayer* start = app.layers[app.marqueeLayerIdx].get();
  if (!start || !start->wlOut || !app.pointerSurface) return false;

  DesktopLayer* under = layer_from_surface(app, app.pointerSurface);
  if (!under || !under->wlOut) return false;

  const auto* sUnder = output_bounds_for(app, under->wlOut);
  const auto* sStart = output_bounds_for(app, start->wlOut);
  if (!sUnder || !sStart) return false;

  const double gx = static_cast<double>(sUnder->global_x) + app.pointerX;
  const double gy = static_cast<double>(sUnder->global_y) + app.pointerY;
  app.marqueeX1 = gx - static_cast<double>(sStart->global_x);
  app.marqueeY1 = gy - static_cast<double>(sStart->global_y);

  if (start->configuredWidth > 0)
    app.marqueeX1 = std::clamp(app.marqueeX1, 0.0, static_cast<double>(start->configuredWidth));
  if (start->configuredHeight > 0)
    app.marqueeY1 = std::clamp(app.marqueeY1, 0.0, static_cast<double>(start->configuredHeight));
  return true;
}

void normalize_marquee_final_rect(double& fx0, double& fy0, double& fx1, double& fy1, int w, int h) {
   
  if (w <= 0 || h <= 0) return;
  if (fx1 - fx0 < 2.0) {
    const double m = (fx0 + fx1) * 0.5;
    fx0 = m - 1.0;
    fx1 = m + 1.0;
  }
  if (fy1 - fy0 < 2.0) {
    const double m = (fy0 + fy1) * 0.5;
    fy0 = m - 1.0;
    fy1 = m + 1.0;
  }
  fx0 = std::clamp(fx0, 0.0, static_cast<double>(std::max(0, w - 2)));
  fx1 = std::clamp(fx1, fx0 + 2.0, static_cast<double>(w));
  fy0 = std::clamp(fy0, 0.0, static_cast<double>(std::max(0, h - 2)));
  fy1 = std::clamp(fy1, fy0 + 2.0, static_cast<double>(h));
}

bool pointer_motion_marquee(DesktopApp& app) {
   
  if (open_with_pointer_motion(app)) return true;
  if (world_clock_settings_pointer_motion(app)) return true;
  if (desktop_mount_dialog_pointer_motion(app)) return true;
  if (desktop_icon_menu_pointer_motion(app)) return true;
  if (desktop_menu_pointer_motion(app)) return true;
  if (desktop_icons_pointer_motion(app)) return true;
  if (!app.marqueeDragging) return false;
  if (marquee_drag_update_end_from_pointer(app)) {
    if (app.marqueeLayerIdx < app.layers.size() && app.layers[app.marqueeLayerIdx])
      paint_layer(app, *app.layers[app.marqueeLayerIdx]);
    wl_display_flush(app.display);
  }
  return true;
}

bool pointer_button_left_release_marquee(DesktopApp& app, bool left_button) {
   
  if (left_button && desktop_mount_dialog_left_release(app)) return true;
  if (left_button && world_clock_settings_left_release(app)) return true;
  if (left_button && desktop_icon_menu_left_release(app)) return true;
  if (left_button && desktop_menu_left_release(app)) return true;
  if (left_button && desktop_icons_left_release(app)) return true;
  if (!left_button || !app.marqueeDragging) return false;
  (void)marquee_drag_update_end_from_pointer(app);
  app.marqueeDragging = false;
  app.marqueeFx0 = std::min(app.marqueeX0, app.marqueeX1);
  app.marqueeFy0 = std::min(app.marqueeY0, app.marqueeY1);
  app.marqueeFx1 = std::max(app.marqueeX0, app.marqueeX1);
  app.marqueeFy1 = std::max(app.marqueeY0, app.marqueeY1);
  if (app.marqueeLayerIdx < app.layers.size() && app.layers[app.marqueeLayerIdx]) {
    DesktopLayer& marqL = *app.layers[app.marqueeLayerIdx];
    normalize_marquee_final_rect(app.marqueeFx0, app.marqueeFy0, app.marqueeFx1, app.marqueeFy1, marqL.configuredWidth, marqL.configuredHeight);
    desktop_icons_select_in_rect(app, app.marqueeLayerIdx, app.marqueeFx0, app.marqueeFy0, app.marqueeFx1, app.marqueeFy1, marqL.configuredWidth,
                                 marqL.configuredHeight);
  }
  app.marqueeVisible = false;
#ifndef NDEBUG
  std::cerr << "[desktop][marquee] left_release: drag ended layer_idx=" << app.marqueeLayerIdx << " final_rect=("
            << app.marqueeFx0 << "," << app.marqueeFy0 << ")-(" << app.marqueeFx1 << "," << app.marqueeFy1
            << ") visible=false destroyed=yes (cleared after drag)\n";
#endif
  paint_all_layers(app);
  if (app.display) wl_display_flush(app.display);
  return true;
}

void desktop_left_press_marquee(DesktopApp& app) {
   
  if (open_with_handle_left_press(app)) return;
  if (world_clock_settings_handle_left_press(app)) return;
  if (desktop_icon_menu_handle_left_press(app)) return;
  if (desktop_menu_handle_left_press(app)) return;
  if (desktop_mount_dialog_handle_left_press(app)) return;
  if (desktop_icons_left_press(app)) return;
  desktop_icons_clear_marquee_selection(app);
  app.marqueeDragging = true;
  app.marqueeLayerIdx = app.pointerLayerIdx;
  double lx = app.pointerX;
  double ly = app.pointerY;
  if (desktop_pointer_local_xy(app, app.marqueeLayerIdx, &lx, &ly)) {
    app.marqueeX0 = lx;
    app.marqueeY0 = ly;
    app.marqueeX1 = lx;
    app.marqueeY1 = ly;
  } else {
    app.marqueeX0 = app.pointerX;
    app.marqueeY0 = app.pointerY;
    app.marqueeX1 = app.pointerX;
    app.marqueeY1 = app.pointerY;
  }
#ifndef NDEBUG
  std::cerr << "[desktop][marquee] drag_start: layer_idx=" << app.marqueeLayerIdx << " origin=(" << app.marqueeX0 << "," << app.marqueeY0
            << ") destroyed=no\n";
#endif
  if (DesktopLayer* dl = layer_from_surface(app, app.pointerSurface)) paint_layer(app, *dl);
  wl_display_flush(app.display);
}

}
