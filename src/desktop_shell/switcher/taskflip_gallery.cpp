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

void paint_gallery(cairo_t* cr, const std::vector<Entry>& entries, std::size_t selected,
                   double w, double h, eh::icons::IconCache& icons, const PaintStyle& st) {
  cairo_set_source_rgba(cr, st.cardR, st.cardG, st.cardB, st.cardA);
  round_rect(cr, 0, 0, w, h, 16.0);
  cairo_fill(cr);
  if (entries.empty()) return;
  if (selected >= entries.size()) selected = 0;

  const double cy = h * 0.42;
  const double baseDia = std::min(96.0, h * 0.45);
  const double stepX = baseDia * 0.72;
  const double midX = w * 0.5;

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, st.fontSize);

  for (std::size_t i = 0; i < entries.size(); ++i) {
    const long di = (long)i - (long)selected;
    const double ad = std::abs((double)di);
    if (ad > 4.0) continue;
    const double scale = 1.0 / (1.0 + ad * 0.28);
    const double dia = baseDia * scale;
    const double cx = midX + di * stepX;
    const double alpha = std::max(0.25, 1.0 - ad * 0.22);
    const bool isSel = (i == selected);

    if (isSel) {
      cairo_set_source_rgba(cr, st.accentR, st.accentG, st.accentB, 0.20);
      round_rect(cr, cx - dia * 0.5 - 10, cy - dia * 0.5 - 10, dia + 20, dia + 20, 14.0);
      cairo_fill(cr);
    }
    {
      const auto* ic = icons.app_icon(entries[i].appId);
      if (ic && ic->surface) {
        const int sw = cairo_image_surface_get_width(ic->surface);
        const int sh = cairo_image_surface_get_height(ic->surface);
        if (sw > 0 && sh > 0) {
          const double s = dia / std::max(sw, sh);
          cairo_save(cr);
          cairo_translate(cr, cx - sw * s * 0.5, cy - sh * s * 0.5);
          cairo_scale(cr, s, s);
          cairo_set_source_surface(cr, ic->surface, 0, 0);
          cairo_paint_with_alpha(cr, alpha);
          cairo_restore(cr);
        }
      }
    }
  }

  const Entry& sel = entries[selected];
  std::string label = sel.title.empty() ? sel.appId : sel.title;
  if (label.size() > 48) {
    label.resize(47);
    label += "\u2026";
  }
  cairo_set_source_rgba(cr, st.textR, st.textG, st.textB, 0.92);
  cairo_text_extents_t ex{};
  cairo_text_extents(cr, label.c_str(), &ex);
  cairo_move_to(cr, midX - ex.width * 0.5 - ex.x_bearing, h * 0.82);
  cairo_show_text(cr, label.c_str());

  char count[64];
  std::snprintf(count, sizeof(count), "%zu / %zu", selected + 1, entries.size());
  cairo_set_source_rgba(cr, st.dimR, st.dimG, st.dimB, 0.6);
  cairo_text_extents(cr, count, &ex);
  cairo_move_to(cr, midX - ex.width * 0.5 - ex.x_bearing, h * 0.82 + 20.0);
  cairo_show_text(cr, count);
}

}
