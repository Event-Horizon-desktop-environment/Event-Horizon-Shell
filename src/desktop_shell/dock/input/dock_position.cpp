#include "desktop_shell/dock/input/dock_position.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/output/dock_layer_outputs.hpp"
#include "desktop_shell/dock/paint/dock_strip_geometry.hpp"
#include "desktop_shell/shared/popup/popup_position.hpp"
#include "desktop_shell/shared/popup/geometry/margins.hpp"
#include "wl/core/protocols.hpp"

#include <algorithm>
#include <cmath>

using eh::shell::dock::dock_pill_geometry_dims;

static int dock_layer_bar_reference_height_px(const DockApp& app) {
  return dock_height_for_icon_px(dock_effective_icon_px(app.settings));
}

int dock_compositor_margin_bottom_px(const DockApp& app) {
   
  const int userGap = std::max(0, std::min(25, app.settings.dockBottomGap));
  if (!app.settings.dockShowDock) return userGap;
  if (userGap <= 0) return 0;
  const int refH = dock_layer_bar_reference_height_px(app);
  const int dh = app.dockHeight;
  return std::max(0, userGap + (refH - dh) / 2);
}

static int dock_layer_origin_x_on_output(const DockApp& app, wl_output* wlo, int layerLogicalW) {
  if (!wlo || layerLogicalW <= 0) return 0;
  for (const auto& u : app.outputSlots) {
    if (u && u->output == wlo && u->logical_w > 0) {
      const double off =
          static_cast<double>(u->logical_w - layerLogicalW) * 0.5;
      return std::max(0, static_cast<int>(std::lround(off)));
    }
  }
  return 0;
}

static int dock_output_logical_w(const DockApp& app, wl_output* wlo) {
  if (!wlo) return 0;
  for (const auto& u : app.outputSlots)
    if (u && u->output == wlo && u->logical_w > 0) return u->logical_w;
  return 0;
}

DockOutputLayer* dock_popup_margin_reference_layer(DockApp& app) {
  if (DockOutputLayer* L = dock_layer_from_surface(app, app.pointerSurface)) return L;
  if (app.pointerDockLayerIdx < app.dockLayers.size() && app.dockLayers[app.pointerDockLayerIdx]) {
    return app.dockLayers[app.pointerDockLayerIdx].get();
  }
  if (!app.dockLayers.empty()) return app.dockLayers[0].get();
  return nullptr;
}

bool dock_popup_compute_layer_margins(DockApp& app, DockOutputLayer* L, int anchorLocalX, int* marginLeft,
                                       int* marginBottom) {
   
  if (!L || !L->layer) return false;

  const int layerH = L->configuredHeight > 0 ? L->configuredHeight : app.dockHeight;
  const int layerW = L->configuredWidth > 0 ? L->configuredWidth : app.configuredWidth;
  double pillX{}, pillY{}, pillW{}, pillH{};
  dock_pill_geometry_dims(app, layerW, layerH, pillX, pillY, pillW, pillH);
  (void)pillX;
  (void)pillW;
  (void)pillH;
  const int pillTop = static_cast<int>(std::ceil(pillY - 1e-9));
  const int bottomClearance = dock_compositor_margin_bottom_px(app) + layerH - pillTop;
  const int originX = dock_layer_origin_x_on_output(app, L->wlOut, layerW);
  const int outW = dock_output_logical_w(app, L->wlOut);

  PopupPositionInput in{};
  in.anchor_x = anchorLocalX;
  in.popup_w = app.popupW;
  in.popup_h = app.popupH;
  in.output_w = outW;
  in.layer_w = layerW;
  in.layer_origin_x = originX;
  in.bottom_clearance = bottomClearance;

  const PopupPositionOutput pos = compute_popup_position(in);
  *marginLeft = pos.margin_left;
  *marginBottom = pos.margin_bottom;
  return true;
}

void dock_popup_sync_layer_margins_if_open(DockApp& app) {
    
  if (!app.popupOpen || !app.popupLayerSurface) return;
  // Fullscreen-grab popups (power confirm) must keep zero margins: the
  // pill-position math would offset the surface by the dock's centering
  // origin and push the dialog off the screen's center.
  if (app.popupKind == DockApp::PopupKind::PowerConfirm) return;
  const int extra = popup_extra_margin(static_cast<int>(app.popupKind));
  int newBottom = 0, newLeft = 0;
  if (app.popupMarginOverride.output) {
    newBottom = app.popupMarginOverride.marginBottom + extra;
    newLeft = app.popupMarginOverride.marginLeft + extra;
  } else {
    DockOutputLayer stackLegacy{};
    DockOutputLayer* L = dock_popup_margin_reference_layer(app);
    if (!L && app.layerSurface && app.dockLayerOutput) {
      stackLegacy.surface = app.surface;
      stackLegacy.layer = app.layerSurface;
      stackLegacy.wlOut = app.dockLayerOutput;
      stackLegacy.configuredWidth = app.configuredWidth;
      stackLegacy.configuredHeight = app.configuredHeight;
      L = &stackLegacy;
    }
    if (!L || !L->wlOut) return;
    int marginLeft = 0, marginBottom = 0;
    if (!dock_popup_compute_layer_margins(app, L, app.popupAnchorX, &marginLeft, &marginBottom)) return;
    newBottom = marginBottom + extra;
    newLeft = marginLeft + extra;
  }
  if (newBottom == app.popupLastMarginBottom && newLeft == app.popupLastMarginLeft) return;
  app.popupLastMarginBottom = newBottom;
  app.popupLastMarginLeft = newLeft;
  zwlr_layer_surface_v1_set_margin(app.popupLayerSurface, 0, 0, newBottom, newLeft);
}
