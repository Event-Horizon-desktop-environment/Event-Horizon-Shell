#include "desktop_shell/controlcenter/widgets/bluetooth/control_center_bluetooth_widget.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint_utils.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"

#include "desktop_shell/common/glyph/material_glyph.hpp"

#include <cstdio>

#include <cairo/cairo.h>

namespace eh::shell::dock::control_center {

using paint_utils::rrect;
using paint_utils::cc_paint_glass_card_mc;
using paint_utils::cc_draw_status_pill;
using paint_utils::kSurfR;
using paint_utils::kSurfG;
using paint_utils::kSurfB;

ControlCenterBluetoothState control_center_bluetooth_state() {
  const auto bs = eh::shell::dock_slot_hooks::bluetooth_snapshot();

  ControlCenterBluetoothState out{};
  out.powered = bs.powered;
  out.connected = bs.connected;
  out.paired_count = bs.paired_count;
  if (!bs.available) out.status_text = "Unavailable";
  else if (!out.powered) out.status_text = "Disabled";
  else if (bs.connected_count > 0) out.status_text = "Connected";
  else out.status_text = "No device";
  return out;
}

void paint_control_center_bluetooth_card(cairo_t* cr, double x, double y, double w, double h,
                                         const ControlCenterBluetoothState& bs, double inner_glass_scale) {
  const auto mc = eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  const double s = std::clamp(inner_glass_scale, 0.0, 1.0);
  const double r = std::max(14.0, std::min(w, h) * 0.16);
  cc_paint_glass_card_mc(cr, x, y, w, h, r, s, mc);

  const double icx = x + 24.0;
  const double icy = y + 28.0;
  cairo_save(cr);
  cairo_arc(cr, icx, icy, 14.0, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, bs.powered ? 0.18 * s : 0.08 * s);
  cairo_fill(cr);
  cairo_restore(cr);

  const char* glyph = bs.powered ? "bluetooth" : "bluetooth_disabled";
  const double gr = bs.powered ? mc.accentR : 0.65, gg = bs.powered ? mc.accentG : 0.68, gb = bs.powered ? mc.accentB : 0.72;
  eh::shell::draw_material_glyph(cr, icx, icy + 1.0, 20.0, glyph, gr, gg, gb, 0.96);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 14.0);
  cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, 0.95);
  cairo_move_to(cr, x + 48.0, y + 30.0);
  cairo_show_text(cr, "Bluetooth");

  cairo_set_font_size(cr, 11.0);
  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.90);
  cairo_move_to(cr, x + 48.0, y + 50.0);
  char line[64];
  std::snprintf(line, sizeof(line), "%d paired device%s", bs.paired_count, bs.paired_count == 1 ? "" : "s");
  cairo_show_text(cr, line);

  cc_draw_status_pill(cr, x + w - 112.0, y + 18.0, bs.powered ? "On" : "Off", bs.powered, s, mc);
}

} // namespace eh::shell::dock::control_center
