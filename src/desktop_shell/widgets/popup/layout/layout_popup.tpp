#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "desktop_shell/widgets/layout/layout_paint.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <cairo/cairo.h>

namespace eh::shell::dock::popup::layout {

namespace {

constexpr double kCornerRadius = 20.0;
constexpr double kIconBoxFull = 38.0;
constexpr double kIconBoxGrid = 30.0;

constexpr double kShadOffX = 2.0;
constexpr double kShadOffY = 4.0;
constexpr double kShadAlpha = 0.28;

void truncate_to_width(cairo_t* cr, const std::string& text, double maxAdv, std::string* out) {
  *out = text;
  cairo_text_extents_t ex{};
  for (;;) {
    cairo_text_extents(cr, out->c_str(), &ex);
    if (ex.x_advance <= maxAdv || out->size() < 4) break;
    // Pop one UTF-8 codepoint.
    size_t i = out->size();
    while (i > 0) {
      --i;
      if ((static_cast<unsigned char>((*out)[i]) & 0xc0u) != 0x80u) {
        out->resize(i);
        break;
      }
    }
    if (i == 0) {
      out->clear();
      break;
    }
  }
}

}  // namespace

template <typename A>
inline void dock_layout_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  double W = 0.0, H = 0.0;
  layout_popup_size(&W, &H);
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const std::string cur = eh::widgets::layout_current();
  const bool grid = layout_popup_columns() == 2;

  cairo_save(cr);
  eh::shell::shared::rounded_rect(cr, kShadOffX, kShadOffY, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, 0, 0, 0, kShadAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  eh::shell::shared::rounded_rect(cr, 0, 0, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, mc.dockFillR * 0.30, mc.dockFillG * 0.30, mc.dockFillB * 0.30,
                        0.92 * sc.appearance.overlayOpacityWidgetCard);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const double px = app.pointerX, py = app.pointerY;

  // Close button.
  double cbX = 0.0, cbY = 0.0;
  layout_popup_close_rect(&cbX, &cbY);
  const bool hc = px >= cbX && px < cbX + 24.0 && py >= cbY && py < cbY + 24.0;
  eh::shell::shared::rounded_rect(cr, cbX, cbY, 24.0, 24.0, 7.0);
  cairo_set_source_rgba(cr, 0.3, 0.3, 0.35, hc ? 0.75 : 0.45);
  cairo_fill(cr);
  eh::shell::draw_material_glyph(cr, cbX + 12.0, cbY + 12.0, 14, "close", mc.textR, mc.textG,
                                 mc.textB, hc ? 1.0 : 0.85);

  // Header.
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.92);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 14);
  cairo_move_to(cr, kLayoutPopupPad, kLayoutPopupPad + 22);
  cairo_show_text(cr, "Window Layout");
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.5);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 11);
  cairo_move_to(cr, kLayoutPopupPad, kLayoutPopupPad + 38);
  cairo_show_text(cr, grid ? "MangoWM tiling layout" : "Hyprland tiling layout");

  const int n = layout_popup_entry_count();
  for (int i = 0; i < n; ++i) {
    double rx = 0.0, ry = 0.0, rw = 0.0, rh = 0.0;
    if (!layout_popup_cell_rect(i, &rx, &ry, &rw, &rh)) continue;
    const char* id = layout_popup_entry_id(i);
    const bool active = (cur == id);
    const double pillR = std::max(2.0, std::min(rh, rw) * 0.22);

    eh::shell::shared::rounded_rect(cr, rx, ry, rw, rh, pillR);
    if (active)
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.22);
    else
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
    cairo_fill(cr);

    const bool hover = px >= rx && px < rx + rw && py >= ry && py < ry + rh;
    if (hover) {
      eh::shell::shared::rounded_rect(cr, rx, ry, rw, rh, pillR);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, active ? 0.28 : 0.18);
      cairo_fill_preserve(cr);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.35);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    } else if (active) {
      eh::shell::shared::rounded_rect(cr, rx, ry, rw, rh, pillR);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.45);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    if (!grid) {
      // Full row: icon tile + title + description + check.
      const double ibX = rx + 9.0;
      const double ibY = ry + (rh - kIconBoxFull) * 0.5;
      eh::shell::shared::rounded_rect(cr, ibX, ibY, kIconBoxFull, kIconBoxFull, 10.0);
      if (active)
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.85);
      else
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
      cairo_fill(cr);
      eh::shell::draw_material_glyph(cr, ibX + kIconBoxFull * 0.5, ibY + kIconBoxFull * 0.5, 20,
                                     eh::widgets::layout_glyph_for(id), 1.0, 1.0, 1.0, active ? 1.0 : 0.9);

      const double tx = ibX + kIconBoxFull + 12.0;
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, active ? 1.0 : 0.92);
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL,
                             active ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 13);
      std::string shown;
      truncate_to_width(cr, eh::widgets::layout_title_for(id), rx + rw - 9.0 - 34.0 - tx, &shown);
      cairo_move_to(cr, tx, ry + 24);
      cairo_show_text(cr, shown.c_str());

      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.5);
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 11);
      cairo_move_to(cr, tx, ry + 41);
      cairo_show_text(cr, eh::widgets::layout_desc_for(id));

      if (active) {
        eh::shell::draw_material_glyph(cr, rx + rw - 9.0 - 16.0, ry + rh * 0.5, 18, "check",
                                       mc.accentR, mc.accentG, mc.accentB, 1.0);
      }
    } else {
      // Compact grid cell: icon tile + title + check.
      const double ibX = rx + 8.0;
      const double ibY = ry + (rh - kIconBoxGrid) * 0.5;
      eh::shell::shared::rounded_rect(cr, ibX, ibY, kIconBoxGrid, kIconBoxGrid, 8.0);
      if (active)
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.85);
      else
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
      cairo_fill(cr);
      eh::shell::draw_material_glyph(cr, ibX + kIconBoxGrid * 0.5, ibY + kIconBoxGrid * 0.5, 17,
                                     eh::widgets::layout_glyph_for(id), 1.0, 1.0, 1.0, active ? 1.0 : 0.9);

      const double tx = ibX + kIconBoxGrid + 8.0;
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, active ? 1.0 : 0.92);
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL,
                             active ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12);
      std::string shown;
      truncate_to_width(cr, eh::widgets::layout_title_for(id), rx + rw - 8.0 - (active ? 24.0 : 0.0) - tx,
                        &shown);
      cairo_text_extents_t te{};
      cairo_text_extents(cr, shown.c_str(), &te);
      cairo_move_to(cr, tx, ry + (rh - te.height) * 0.5 - te.y_bearing);
      cairo_show_text(cr, shown.c_str());

      if (active) {
        eh::shell::draw_material_glyph(cr, rx + rw - 8.0 - 9.0, ry + rh * 0.5, 15, "check",
                                       mc.accentR, mc.accentG, mc.accentB, 1.0);
      }
    }
  }
}

inline int layout_popup_height() {
  double W = 0.0, H = 0.0;
  layout_popup_size(&W, &H);
  return static_cast<int>(std::ceil(H));
}

}  // namespace eh::shell::dock::popup::layout
