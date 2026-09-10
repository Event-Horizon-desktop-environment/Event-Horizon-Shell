#include "desktop_shell/shared/popup/paint/paint.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/common/os_logo/os_logo.hpp"
#include "desktop_shell/shared/popup/geometry/layout.hpp"
#include "desktop_shell/spotlight/paint/spotlight_paint.hpp"
#include "desktop_shell/common/time/text_caret.hpp"

#include <cairo/cairo.h>
#include <cmath>

#include "configuration/shell_config.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"

using eh::shell::dock::kSpotlightRowPx;
using eh::shell::dock::kSpotlightSearchOuterH;
using eh::shell::dock::spotlight_truncate_to_width;

void dock_popup_paint_spotlight(DockApp& app, cairo_t* cr, const eh::config::ShellConfig& scPopupOv) {
   
if (!app.distroSpotlightLogo) app.distroSpotlightLogo = eh_os_logo::load_distro_logo_cairo_surface();
const double W = static_cast<double>(app.popupW);
const double H = static_cast<double>(app.popupH);
const double us = dock_ui_scale(app.settings);
const double kEdge = 12.0 * us;
auto round_rect = [&](double rx, double ry, double rw, double rh, double rr) {
  const double rad = std::min({rr, rw * 0.5, rh * 0.5});
  const double xL = rx;
  const double yT = ry;
  const double xR = rx + rw;
  const double yB = ry + rh;
  cairo_new_path(cr);
  cairo_arc(cr, xR - rad, yT + rad, rad, -M_PI_2, 0);
  cairo_arc(cr, xR - rad, yB - rad, rad, 0, M_PI_2);
  cairo_arc(cr, xL + rad, yB - rad, rad, M_PI_2, M_PI);
  cairo_arc(cr, xL + rad, yT + rad, rad, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
};
const double shellOv = static_cast<double>(eh::config::overlay_surface_alpha_scale(
    scPopupOv, eh::config::OverlaySurfaceAlphaKind::AppDrawer));
const eh::config::ChromePaintColors mcSpot = eh::config::derived_chrome_colors(scPopupOv.appearance);
eh::shell::shared::paint_glass_card(cr, 0, 0, W, H, 16.0 * us, mcSpot, shellOv);
const double pillX = kEdge;
const double pillY = kEdge;
const double pillW = W - 2.0 * kEdge;
const double pillH = static_cast<double>(kSpotlightSearchOuterH()) - 2.0 * kEdge;
{
  m3::Box pillBox;
  pillBox.setColor(static_cast<float>(mcSpot.dockFillR * 0.6 + mcSpot.accentR * 0.15),
                   static_cast<float>(mcSpot.dockFillG * 0.6 + mcSpot.accentG * 0.15),
                   static_cast<float>(mcSpot.dockFillB * 0.6 + mcSpot.accentB * 0.15),
                   static_cast<float>(0.78 * shellOv));
  pillBox.setRadius(static_cast<float>(24.0 * us));
  pillBox.setGeometry(static_cast<float>(pillX), static_cast<float>(pillY),
                      static_cast<float>(pillW), static_cast<float>(pillH));
  pillBox.setGlassy(true);
  pillBox.paint(cr);
}
if (app.distroSpotlightLogo) {
  const int lw = cairo_image_surface_get_width(app.distroSpotlightLogo);
  const int lh = cairo_image_surface_get_height(app.distroSpotlightLogo);
  const double target = 28.0 * us;
  const double sc = target / static_cast<double>(std::max(1, std::max(lw, lh)));
  const double dh = static_cast<double>(lh) * sc;
  const double lx = pillX + 12.0 * us;
  const double ly = pillY + (pillH - dh) * 0.5;
  cairo_save(cr);
  cairo_translate(cr, lx, ly);
  cairo_scale(cr, sc, sc);
  cairo_set_source_surface(cr, app.distroSpotlightLogo, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);
}
cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
cairo_set_font_size(cr, 17 * us);
const double tx = pillX + (app.distroSpotlightLogo ? 46.0 * us : 14.0 * us);
const double textRight = pillX + pillW - 10.0 * us;
const double maxTextW = textRight - tx;
cairo_save(cr);
cairo_rectangle(cr, tx, pillY, std::max(1.0, maxTextW), pillH);
cairo_clip(cr);
const uint64_t spotMono = eh::shell::monotonic_ms();
const bool spotCaretOn = eh::shell::text_caret_blink_on(spotMono, true);
double spotCaretX = tx;
if (app.spotlightQuery.empty()) {
  cairo_set_source_rgba(cr, 0.92, 0.95, 0.95, 1.0);
  cairo_move_to(cr, tx, pillY + pillH * 0.5 + 6.0 * us);
  cairo_show_text(cr, "Search…");
} else {
  std::string shown;
  spotlight_truncate_to_width(cr, app.spotlightQuery, maxTextW, &shown);
  cairo_set_source_rgba(cr, 0.93, 0.96, 0.96, 1.0);
  cairo_move_to(cr, tx, pillY + pillH * 0.5 + 6.0 * us);
  cairo_show_text(cr, shown.c_str());
  cairo_text_extents_t sEx{};
  cairo_text_extents(cr, shown.c_str(), &sEx);
  spotCaretX = tx + sEx.x_advance + 1.5 * us;
}
if (spotCaretOn) {
  const double midY = pillY + pillH * 0.5;
  cairo_move_to(cr, spotCaretX, midY - 10.0 * us);
  cairo_line_to(cr, spotCaretX, midY + 10.0 * us);
  cairo_set_source_rgba(cr, 0.88, 0.93, 0.96, 1.0);
  cairo_set_line_width(cr, 1.5);
  cairo_stroke(cr);
  cairo_set_line_width(cr, 1.0);
}
cairo_restore(cr);

const double listTop = static_cast<double>(kSpotlightSearchOuterH());
cairo_move_to(cr, kEdge, listTop - 0.5);
cairo_line_to(cr, W - kEdge, listTop - 0.5);
cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
cairo_set_line_width(cr, 1.0);
cairo_stroke(cr);

const double rowOriginY = listTop;
cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
cairo_set_font_size(cr, 13 * us);
const double iconPad = 36.0 * us;
const double textX0 = pillX + iconPad;
const double rowLabelMaxW = W - textX0 - kEdge;

if (!app.spotlightHits.empty()) {
  for (size_t i = 0; i < app.spotlightHits.size(); i++) {
    const double ry = rowOriginY + static_cast<double>(i) * static_cast<double>(kSpotlightRowPx());
    const int ri = static_cast<int>(i);
    if (ri == app.spotlightSel) {
      round_rect(kEdge + 4.0 * us, ry + 4.0 * us, pillW - 8.0 * us, static_cast<double>(kSpotlightRowPx()) - 8.0 * us, 8.0 * us);
      cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
      cairo_fill(cr);
    }
    const SpotlightHit& sh = app.spotlightHits[i];
    if (const eh::icons::IconEntry* ic = app.icons.tray_icon(sh.iconKey)) {
      if (ic->surface) {
        const double iconTarget = 22.0 * us;
        const double ix = pillX + 8.0 * us;
        const double iy = ry + 0.5 * static_cast<double>(kSpotlightRowPx()) - iconTarget * 0.5;
        cairo_save(cr);
        cairo_translate(cr, ix, iy);
        const double iw = static_cast<double>(ic->width);
        const double ih = static_cast<double>(ic->height);
        const double mx = std::max(1.0, std::max(iw, ih));
        const double sc2 = iconTarget / mx;
        cairo_scale(cr, sc2, sc2);
        cairo_set_source_surface(cr, ic->surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
      }
    }
    cairo_set_source_rgba(cr, 0.93, 0.96, 0.96, 1.0);
    const double textBaseline = ry + 22.0 * us;
    cairo_move_to(cr, textX0, textBaseline);
    std::string line = sh.name;
    const std::string sub = !sh.comment.empty() ? sh.comment : sh.genericName;
    if (!sub.empty()) line += std::string(" — ") + sub;
    std::string shown;
    spotlight_truncate_to_width(cr, line, rowLabelMaxW, &shown);
    cairo_show_text(cr, shown.c_str());
  }
} else if (!app.spotlightQuery.empty()) {
  cairo_set_source_rgba(cr, 0.55, 0.6, 0.62, 0.95);
  cairo_move_to(cr, textX0, rowOriginY + 24.0 * us);
  cairo_show_text(cr, "No matching applications");
}

}
