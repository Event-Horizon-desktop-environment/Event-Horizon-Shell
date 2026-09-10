#include "desktop_shell/shared/popup/paint/paint.hpp"

#include "desktop_shell/dock/core/dock_app.h"

#include <cairo/cairo.h>

#include "configuration/shell_config.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"

void dock_popup_paint_context_menu(DockApp& app, cairo_t* cr, const eh::config::ShellConfig& scPopupOv) {
 
const double menuShellOv = (app.popupKind == DockApp::PopupKind::Tray)
                               ? static_cast<double>(eh::config::overlay_surface_alpha_scale(
                                     scPopupOv, eh::config::OverlaySurfaceAlphaKind::TrayMenu))
                               : static_cast<double>(eh::config::overlay_surface_alpha_scale(
                                     scPopupOv, eh::config::OverlaySurfaceAlphaKind::DockContextMenu));
const eh::config::ChromePaintColors mcMenu = eh::config::derived_chrome_colors(scPopupOv.appearance);

const double us = dock_ui_scale(app.settings);
const double radius = 10.0 * us;
auto rounded_rect = [&](double rx, double ry, double rw, double rh, double r) {
  const double x0 = rx, y0 = ry, x1 = rx + rw, y1 = ry + rh;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x1 - r, y0 + r, r, -M_PI_2, 0);
  cairo_arc(cr, x1 - r, y1 - r, r, 0, M_PI_2);
  cairo_arc(cr, x0 + r, y1 - r, r, M_PI_2, M_PI);
  cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
};

eh::shell::shared::paint_glass_card(cr, 0, 0, static_cast<double>(app.popupW),
                                    static_cast<double>(app.popupH), radius, mcMenu, menuShellOv);

cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
cairo_set_font_size(cr, 13 * us);

const int padX = 16 * us;
const int rowH = 26 * us;
int y = 4 * us;
for (int i = 0; i < static_cast<int>(app.popupItems.size()); ++i) {
  const auto& it = app.popupItems[i];
  if (it.id < 0) {
    cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, 16 * us, y + rowH / 2);
    cairo_line_to(cr, app.popupW - 16 * us, y + rowH / 2);
    cairo_stroke(cr);
    y += rowH / 2;
    continue;
  }

  if (i == app.popupHoverItem && it.enabled) {
    rounded_rect(4 * us, y, app.popupW - 8.0 * us, rowH, 6.0 * us);
    cairo_set_source_rgba(cr, mcMenu.accentR, mcMenu.accentG, mcMenu.accentB, 0.16);
    cairo_fill(cr);
  }
  cairo_set_source_rgba(cr, 1, 1, 1, it.enabled ? 0.92 : 0.45);
  cairo_move_to(cr, padX, y + 18 * us);
  cairo_show_text(cr, it.label.c_str());
  y += rowH;
}

}
