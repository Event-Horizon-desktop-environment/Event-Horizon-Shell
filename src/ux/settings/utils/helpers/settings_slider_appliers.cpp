#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "m3/core/primitives/box.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/common/settings_common.hpp"

double slider_norm_from_x(double px, int trX, int trW) noexcept {
   
  if (trW <= 0) return 0.0;
  const double t = (px - static_cast<double>(trX)) / static_cast<double>(trW);
  return std::clamp(t, 0.0, 1.0);
}

static std::string settings_truncate_plain(const std::string& s, size_t maxChars) {
  if (s.size() <= maxChars) return s;
  if (maxChars <= 3) return s.substr(0, maxChars);
  return s.substr(0, maxChars - 3) + "...";
}

void settings_draw_trimmed_text_line(cairo_t* cr, const std::string& text, double x, double baselineY,
                                            size_t approxMaxChars, double fadeAlpha, float fontSize, int fontWeight) {
  const std::string t = settings_truncate_plain(text, approxMaxChars);
  settings_show_text(cr, x, baselineY, t.c_str(), fontSize, fontWeight, Theme::TextR, Theme::TextG, Theme::TextB, static_cast<float>(fadeAlpha));
}

void settings_content_column_geom(const App& app, int* contentX, int* contentW) {
  *contentX = kSpacingL + kSidebarW + kSpacingL;
  *contentW = app.width - *contentX - kSpacingL;
}

int settings_content_viewport_h(const App& app) {
  return std::max(120, app.height - kContentTop - kSpacingL);
}

int settings_mode_dd_pointer_row(double px, double py, int listLeft, int listTop, int listW, int rowH,
                                        int rowCount) {
  const int listH = rowCount * rowH;
  if (px < listLeft || py < listTop || px >= listLeft + listW || py >= listTop + listH) return -1;
  const int r = static_cast<int>((py - listTop) / rowH);
  if (r < 0 || r >= rowCount) return -1;
  return r;
}

void settings_paint_combo_list_popup(App& app, cairo_t* cr, int x, int y, int w, int rowH, int rowCount,
                                            const char* const* labels, int selectedIdx, int hoverRow, double glassOv,
                                            int scrollPx, int viewPortH, bool opaquePanel) {
  (void)glassOv;
  const int fullH = rowCount * rowH;
  const int listH = (viewPortH > 0) ? viewPortH : fullH;
  {
    m3::Box box;
    float r, g, b;
    if (app.drawChromeMatugen) {
      r = app.drawChrome.panelFillR;
      g = app.drawChrome.panelFillG;
      b = app.drawChrome.panelFillB;
    } else {
      r = static_cast<float>(Theme::BgR);
      g = static_cast<float>(Theme::BgG);
      b = static_cast<float>(Theme::BgB);
    }
    box.setColor(r, g, b, 1.0f);
    box.setRadius(8.0f);
    box.setGeometry(static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(listH));
    box.paint(cr);
  }
  if (!opaquePanel) {
    cairo_round_rect(cr, x, y, w, listH, 8.0);
    paint_src_glass_hi(app, cr, 0.28);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  }

  cairo_save(cr);
  cairo_rectangle(cr, x, y, w, listH);
  cairo_clip(cr);
  for (int i = 0; i < rowCount; ++i) {
    const int ry = y + i * rowH - scrollPx;
    if (ry + rowH <= y || ry >= y + listH) continue;
    if (i == hoverRow) {
      cairo_rectangle(cr, x + 1, ry + 1, w - 2, rowH - 2);
      paint_src_glass_hi(app, cr, 0.16);
      cairo_fill(cr);
    }
    const bool sel = i == selectedIdx;
    const double textA = sel ? 1.0 : (i == hoverRow ? 0.92 : 0.82);
    settings_show_text(cr, x + 12, ry + 19, labels[i], 12.f, sel ? 700 : 400, Theme::TextR, Theme::TextG, Theme::TextB, static_cast<float>(textA));
  }
  cairo_restore(cr);
}

int settings_measure_max_text_advance_px_sans12(const std::vector<std::string>& labels) {
  if (labels.empty()) return 0;
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t* cr = cairo_create(surf);
  cairo_set_font_size(cr, 12);
  double mx = 0;
  for (int pass = 0; pass < 2; ++pass) {
    const cairo_font_weight_t wt = (pass == 0) ? CAIRO_FONT_WEIGHT_NORMAL : CAIRO_FONT_WEIGHT_BOLD;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, wt);
    for (const auto& s : labels) {
      cairo_text_extents_t te{};
      cairo_text_extents(cr, s.c_str(), &te);
      mx = std::max(mx, te.x_advance);
    }
  }
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return static_cast<int>(std::ceil(mx));
}
