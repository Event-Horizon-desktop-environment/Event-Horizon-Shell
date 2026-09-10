#include "desktop_shell/taskbar/layout/taskbar_position.hpp"

#include "desktop_shell/taskbar/core/taskbar.hpp"
#include "desktop_shell/shared/popup/popup_position.hpp"

#include <algorithm>

namespace eh::shell::taskbar {

TaskbarOutputLayer* taskbar_ref_layer(TaskbarApp& app) {
   
  for (auto& up : app.layers)
    if (up && up->configured) return up.get();
  if (!app.layers.empty()) return app.layers[0].get();
  return nullptr;
}

int taskbar_layer_w(TaskbarApp& app) {
  if (auto* ref = taskbar_ref_layer(app))
    return ref->configuredWidth > 0 ? ref->configuredWidth : 0;
  return 0;
}

TaskbarPopupPositionOutput taskbar_compute_popup_position(TaskbarApp& app, int anchorX, int popupW, int popupH) {
  TaskbarPopupPositionOutput out{};

  auto* ref = taskbar_ref_layer(app);
  if (!ref || !ref->wlOut) return out;
  out.wl_out = ref->wlOut;

  const int layerW = taskbar_layer_w(app);
  const int clearance =
      (app.settings.widthMode == 0 ? app.settings.floatingAmount : 0) + app.settings.height + (app.settings.widthMode == 0 ? 6 : 0);

  PopupPositionInput in{};
  in.anchor_x = anchorX;
  in.popup_w = popupW;
  in.popup_h = popupH;
  in.output_w = 0;
  in.layer_w = layerW;
  in.layer_origin_x = 0;
  in.bottom_clearance = clearance;

  const PopupPositionOutput pos = compute_popup_position(in);

  out.margin_left = pos.margin_left;
  out.margin_bottom = pos.margin_bottom;
  out.clearance = clearance;
  return out;
}

}
