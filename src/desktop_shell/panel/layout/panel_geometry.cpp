// Panel geometry — brand-new layer placement for floating / edge-to-edge / fill.

#include "desktop_shell/panel/layout/panel_geometry.hpp"

#include "desktop_shell/panel/core/panel.hpp"
#include "desktop_shell/shared/popup/popup_position.hpp"
#include "configuration/shell_config.hpp"

namespace eh::shell::panel {

PanelOutputLayer* panel_ref_layer(PanelApp& app) {
  for (auto& up : app.layers)
    if (up && up->configured) return up.get();
  if (!app.layers.empty()) return app.layers[0].get();
  return nullptr;
}

int panel_layer_w(PanelApp& app) {
  if (auto* ref = panel_ref_layer(app)) return ref->configuredWidth > 0 ? ref->configuredWidth : 0;
  return 0;
}

PanelPopupPositionOutput panel_compute_popup_position(PanelApp& app, int anchorX, int popupW, int popupH) {
  PanelPopupPositionOutput out{};
  auto* ref = panel_ref_layer(app);
  if (!ref || !ref->wlOut) return out;
  out.wl_out = ref->wlOut;
  const int layerW = panel_layer_w(app);
  const int clearance = panel_popup_clearance_px(app.settings);
  PopupPositionInput in{};
  in.anchor_x = anchorX;
  in.popup_w = popupW;
  in.popup_h = popupH;
  in.output_w = 0;
  in.layer_w = layerW;
  in.layer_origin_x = 0;
  in.bottom_clearance = clearance;
  const PopupPositionOutput pos = compute_popup_position(in);
  out.margin_side = pos.margin_left;
  out.margin_edge = app.settings.positionTop ? clearance : pos.margin_bottom;
  out.clearance = clearance;
  return out;
}

void panel_layer_apply_geometry(PanelApp& app, PanelOutputLayer& layer) {
  if (!layer.layer) return;
  const PanelSettings& s = app.settings;
  std::uint32_t anchor = s.positionTop ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
  anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  zwlr_layer_surface_v1_set_anchor(layer.layer, anchor);
  const std::uint32_t edge = static_cast<std::uint32_t>(panel_edge_gap_px(s));
  const std::uint32_t floatInset =
      s.widthMode == 0 ? static_cast<std::uint32_t>(s.floatingAmount) + edge : edge;
  if (s.positionTop)
    zwlr_layer_surface_v1_set_margin(layer.layer, floatInset, 0, 0, 0);
  else
    zwlr_layer_surface_v1_set_margin(layer.layer, 0, 0, floatInset, 0);
  const int barH = s.widthMode == 0 ? s.height + 4 : s.height;
  // reserveSpace=false turns the bar into an overlay: no compositor gap.
  // Auto-hide/smart retract the gap while hidden (like the dock).
  const int exZone =
      !s.enabled || !s.reserveSpace ? 0 : (s.autoHide || s.smartAutoHide ? 0 : barH + panel_exclusive_zone_gap_px(s));
  zwlr_layer_surface_v1_set_exclusive_zone(layer.layer, exZone);
  zwlr_layer_surface_v1_set_size(layer.layer, 0, static_cast<std::uint32_t>(barH));
}

}  // namespace eh::shell::panel
