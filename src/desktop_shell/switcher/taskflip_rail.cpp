#include "desktop_shell/switcher/taskflip_paint.hpp"
#include "desktop_shell/switcher/taskflip_model.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"

#include <algorithm>
#include <cmath>

namespace eh::shell::taskflip {

namespace {

void round_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

}

void paint_rail(cairo_t* cr, const std::vector<Entry>& entries, std::size_t selected,
                double w, double h, eh::icons::IconCache& icons, const PaintStyle& st) {
  cairo_set_source_rgba(cr, st.cardR, st.cardG, st.cardB, st.cardA);
  round_rect(cr, 0, 0, w, h, 16.0);
  cairo_fill(cr);
  if (entries.empty()) return;
  if (selected >= entries.size()) selected = 0;

  const double pad = 12.0;
  const double gap = 8.0;
  const double cellH = h - pad * 2.0;
  const double availW = w - pad * 2.0;
  const std::size_t n = entries.size();
  const double cellW = std::min(220.0, (availW - gap * (n - 1)) / (double)n);
  const double totalW = cellW * n + gap * (n - 1);
  double x0 = (w - totalW) * 0.5;

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, st.fontSize);

  for (std::size_t i = 0; i < n; ++i) {
    const double cx = x0 + i * (cellW + gap);
    const bool isSel = (i == selected);
    if (isSel) {
      cairo_set_source_rgba(cr, st.accentR, st.accentG, st.accentB, 0.22);
      round_rect(cr, cx, pad, cellW, cellH, 10.0);
      cairo_fill(cr);
    }
    const double iconDia = std::min(32.0, cellH - 16.0);
    const double ix = cx + 10.0 + iconDia * 0.5;
    const double iy = pad + cellH * 0.5;
    const auto* ic = icons.app_icon(entries[i].appId);
    if (ic && ic->surface) {
      const int sw = cairo_image_surface_get_width(ic->surface);
      const int sh = cairo_image_surface_get_height(ic->surface);
      if (sw > 0 && sh > 0) {
        const double s = iconDia / std::max(sw, sh);
        cairo_save(cr);
        cairo_translate(cr, ix - sw * s * 0.5, iy - sh * s * 0.5);
        cairo_scale(cr, s, s);
        cairo_set_source_surface(cr, ic->surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
      }
    }
    std::string label = entries[i].title.empty() ? entries[i].appId : entries[i].title;
    const double textX = cx + 10.0 + iconDia + 8.0;
    const double maxW = cx + cellW - 8.0 - textX;
    if (maxW > 20.0) {
      cairo_text_extents_t ex{};
      cairo_text_extents(cr, label.c_str(), &ex);
      while (ex.width > maxW && label.size() > 4) {
        label.resize(label.size() - 2);
        label += "\u2026";
        cairo_text_extents(cr, label.c_str(), &ex);
      }
      cairo_set_source_rgba(cr, st.textR, st.textG, st.textB, isSel ? 0.95 : 0.75);
      cairo_move_to(cr, textX, pad + cellH * 0.5 + 4.5);
      cairo_show_text(cr, label.c_str());
    }
  }
}

}
