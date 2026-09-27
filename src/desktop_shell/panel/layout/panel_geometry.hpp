#pragma once

// Panel geometry — brand-new layer-shell placement for the three width modes.
//
//  - Floating:    inset pill, `floatingAmount` px from each side (+ edgeGap),
//                 slightly shorter than the layer so it floats.
//  - Edge-to-edge: full-bleed strip, only `edgeGap` from the screen edge.
//  - Fill:        content-sized centered pill (measured from widget widths).
// The anchor is top or bottom (GNOME-style top by default). Exclusive zone
// reserves the painted height (+ exclusiveZoneGap) unless auto-hide is on.

#include <cstdint>

namespace eh::shell::panel {

struct PanelApp;
struct PanelOutputLayer;

struct PanelPopupPositionOutput {
  void* wl_out = nullptr;
  int margin_edge = 0;
  int margin_side = 0;
  int clearance = 0;
};

PanelOutputLayer* panel_ref_layer(PanelApp& app);
int panel_layer_w(PanelApp& app);
PanelPopupPositionOutput panel_compute_popup_position(PanelApp& app, int anchorX, int popupW, int popupH);
void panel_layer_apply_geometry(PanelApp& app, PanelOutputLayer& layer);

}  // namespace eh::shell::panel
