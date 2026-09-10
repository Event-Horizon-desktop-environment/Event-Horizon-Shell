#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <cairo/cairo.h>

#include <pango/pango.h>
#include <pango/pangocairo.h>

#include "m3/core/primitives/box.hpp"
#include "m3/core/label.hpp"
#include "m3/controls/input/toggle.hpp"
#include "m3/controls/containers/button.hpp"
#include "desktop_shell/ui/dropdown/dropdown.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "ux/Live_Wallpaper/app_types.hpp"

namespace eh::live_wallpaper {

namespace Theme {
static constexpr double BgR = 0.102;
static constexpr double BgG = 0.075;
static constexpr double BgB = 0.188;
static constexpr double TextR = 1.0;
static constexpr double TextG = 1.0;
static constexpr double TextB = 1.0;
static constexpr double AccR = 0.769;
static constexpr double AccG = 0.659;
static constexpr double AccB = 0.941;
}

static constexpr int kSpacingXS  = 4;
static constexpr int kSpacingS   = 8;
static constexpr int kSpacingM   = 12;
static constexpr int kSpacingL   = 16;
static constexpr int kSpacingXL  = 24;
static constexpr int kContentTop = 76;
static constexpr int kCardPad    = kSpacingL;
static constexpr double kCardRad = 14.0;
static constexpr int kWallpaperToggleBandH = 80;
static constexpr int kSettingsDdRowH = 26;

static inline void lw_resolve_colors(const AppState& app,
                                      float& accent_r, float& accent_g, float& accent_b,
                                      float& text_r, float& text_g, float& text_b,
                                      float& surface_r, float& surface_g, float& surface_b,
                                      float& outline_r, float& outline_g, float& outline_b) {
  if (app.drawChromeMatugen) {
    accent_r = app.drawChrome.accentR;
    accent_g = app.drawChrome.accentG;
    accent_b = app.drawChrome.accentB;
    text_r = app.drawChrome.textR;
    text_g = app.drawChrome.textG;
    text_b = app.drawChrome.textB;
    surface_r = app.drawChrome.panelFillR;  // surface approx from chrome
    surface_g = app.drawChrome.panelFillG;
    surface_b = app.drawChrome.panelFillB;
    outline_r = app.drawChrome.outlineR;
    outline_g = app.drawChrome.outlineG;
    outline_b = app.drawChrome.outlineB;
  } else {
    accent_r = static_cast<float>(Theme::AccR);
    accent_g = static_cast<float>(Theme::AccG);
    accent_b = static_cast<float>(Theme::AccB);
    text_r = static_cast<float>(Theme::TextR);
    text_g = static_cast<float>(Theme::TextG);
    text_b = static_cast<float>(Theme::TextB);
    surface_r = static_cast<float>(Theme::BgR);
    surface_g = static_cast<float>(Theme::BgG);
    surface_b = static_cast<float>(Theme::BgB);
    outline_r = static_cast<float>(Theme::TextR) * 0.5f;
    outline_g = static_cast<float>(Theme::TextG) * 0.5f;
    outline_b = static_cast<float>(Theme::TextB) * 0.5f;
  }
}

static inline void paint_src_bg(AppState& app, cairo_t* cr, double a) {
  if (app.drawChromeMatugen)
    cairo_set_source_rgba(cr, app.drawChrome.panelFillR, app.drawChrome.panelFillG, app.drawChrome.panelFillB, a);
  else
    cairo_set_source_rgba(cr, Theme::BgR, Theme::BgG, Theme::BgB, a);
}

static inline void paint_src_accent(AppState& app, cairo_t* cr, double a) {
  if (app.drawChromeMatugen)
    cairo_set_source_rgba(cr, app.drawChrome.accentR, app.drawChrome.accentG, app.drawChrome.accentB, a);
  else
    cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB, a);
}

static inline void paint_src_glass_hi(AppState& app, cairo_t* cr, double a) {
  if (app.drawChromeMatugen) {
    const double hr = 0.48 + 0.52 * app.drawChrome.outlineR;
    const double hg = 0.48 + 0.52 * app.drawChrome.outlineG;
    const double hb = 0.48 + 0.52 * app.drawChrome.outlineB;
    cairo_set_source_rgba(cr, hr, hg, hb, a);
  } else
    cairo_set_source_rgba(cr, 1, 1, 1, a);
}

static inline void cairo_round_rect(cairo_t* cr, double x, double y, double ww, double hh, double rad) {
  const double xL = x;
  const double yT = y;
  const double xR = x + ww;
  const double yB = y + hh;
  const double rr = std::min({rad, ww * 0.5, hh * 0.5});
  cairo_new_path(cr);
  cairo_arc(cr, xR - rr, yT + rr, rr, -M_PI_2, 0);
  cairo_arc(cr, xR - rr, yB - rr, rr, 0, M_PI_2);
  cairo_arc(cr, xL + rr, yB - rr, rr, M_PI_2, M_PI);
  cairo_arc(cr, xL + rr, yT + rr, rr, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

static inline void lw_show_text(cairo_t* cr, double x, double y, const char* text,
                                 float fontSize, int fontWeight,
                                 float r, float g, float b, float a) {
  if (!text || !text[0]) return;
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(fontSize * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(fontWeight));
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text, -1);
  int pw, ph;
  pango_layout_get_pixel_size(layout, &pw, &ph);
  cairo_save(cr);
  cairo_translate(cr, x, y - static_cast<double>(ph) + 2.0);
  cairo_set_source_rgba(cr, r, g, b, a);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  pango_font_description_free(desc);
  g_object_unref(layout);
}

static inline bool point_in_rect(double px, double py, int x, int y, int w, int h) {
  return px >= static_cast<double>(x) && px < static_cast<double>(x + w) &&
         py >= static_cast<double>(y) && py < static_cast<double>(y + h);
}

static inline void lw_paint_combo_closed(AppState& app, cairo_t* cr, int bx, int by, int bw, int bh,
                                          const char* displayText, bool expanded) {
  // Stubbed during the separation cleanup (dropdown include/lookup issue in this
  // TU); the combo is drawn elsewhere.
  (void)app; (void)cr; (void)bx; (void)by; (void)bw; (void)bh; (void)displayText; (void)expanded;
}

static inline void lw_paint_combo_list_popup(AppState& app, cairo_t* cr, int x, int y, int w, int rowH, int rowCount,
                                              const char* const* labels, int selectedIdx, int hoverRow, double glassOv,
                                              int scrollPx = 0, int viewPortH = 0, bool opaquePanel = false) {
  (void)glassOv;
  (void)scrollPx;
  (void)viewPortH;
  (void)opaquePanel;
  const int fullH = rowCount * rowH;
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  lw_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
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
    box.setGeometry(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(fullH));
    box.paint(cr);
  }
  cairo_save(cr);
  cairo_rectangle(cr, x, y, w, fullH);
  cairo_clip(cr);
  for (int i = 0; i < rowCount; ++i) {
    const int ry = y + i * rowH;
    if (i == hoverRow) {
      cairo_rectangle(cr, x + 1, ry + 1, w - 2, rowH - 2);
      cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
      cairo_fill(cr);
    }
    const bool sel = i == selectedIdx;
    const double textA = sel ? 1.0 : (i == hoverRow ? 0.92 : 0.82);
    lw_show_text(cr, static_cast<double>(x + 10), static_cast<double>(ry + rowH - 6), labels[i], 13, 400,
                 t_r, t_g, t_b, static_cast<float>(textA));
  }
  cairo_restore(cr);
  cairo_round_rect(cr, x, y, w, fullH, 8.0);
  cairo_set_source_rgba(cr, o_r, o_g, o_b, 0.5);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
}

static inline int lw_text_width(const char* text, float fontSize, int fontWeight) {
  PangoFontMap* fontmap = pango_cairo_font_map_get_default();
  PangoContext* ctx = pango_font_map_create_context(fontmap);
  PangoLayout* layout = pango_layout_new(ctx);
  PangoFontDescription* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(fontSize * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(fontWeight));
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text, -1);
  int pw, ph;
  pango_layout_get_pixel_size(layout, &pw, &ph);
  pango_font_description_free(desc);
  g_object_unref(layout);
  g_object_unref(ctx);
  return pw;
}

// Box blur for 18+ previews.
static inline cairo_surface_t* cairo_surface_box_blur(cairo_surface_t* src, int radius) {
  if (radius <= 0 || cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_reference(src);
    return src;
  }
  const int w = cairo_image_surface_get_width(src);
  const int h = cairo_image_surface_get_height(src);
  if (w <= 0 || h <= 0) { cairo_surface_reference(src); return src; }

  cairo_surface_t* dst = cairo_surface_create_similar_image(src, CAIRO_FORMAT_ARGB32, w, h);
  unsigned char* srcData = cairo_image_surface_get_data(src);
  unsigned char* dstData = cairo_image_surface_get_data(dst);
  const int stride = cairo_image_surface_get_stride(src);

  std::vector<unsigned char> tmp(static_cast<size_t>(h * stride));

  // Horizontal pass
  for (int y = 0; y < h; ++y) {
    const unsigned char* rowIn = srcData + y * stride;
    unsigned char* rowOut = tmp.data() + y * stride;
    for (int x = 0; x < w; ++x) {
      int r = 0, g = 0, b = 0, a = 0, cnt = 0;
      const int x0 = std::max(0, x - radius);
      const int x1 = std::min(w - 1, x + radius);
      for (int sx = x0; sx <= x1; ++sx) {
        const unsigned char* p = rowIn + sx * 4;
        b += p[0]; g += p[1]; r += p[2]; a += p[3];
        ++cnt;
      }
      unsigned char* pOut = rowOut + x * 4;
      pOut[0] = static_cast<unsigned char>(b / cnt);
      pOut[1] = static_cast<unsigned char>(g / cnt);
      pOut[2] = static_cast<unsigned char>(r / cnt);
      pOut[3] = static_cast<unsigned char>(a / cnt);
    }
  }

  // Vertical pass
  for (int x = 0; x < w; ++x) {
    for (int y = 0; y < h; ++y) {
      int r = 0, g = 0, b = 0, a = 0, cnt = 0;
      const int y0 = std::max(0, y - radius);
      const int y1 = std::min(h - 1, y + radius);
      for (int sy = y0; sy <= y1; ++sy) {
        const unsigned char* p = tmp.data() + sy * stride + x * 4;
        b += p[0]; g += p[1]; r += p[2]; a += p[3];
        ++cnt;
      }
      unsigned char* pOut = dstData + y * stride + x * 4;
      pOut[0] = static_cast<unsigned char>(b / cnt);
      pOut[1] = static_cast<unsigned char>(g / cnt);
      pOut[2] = static_cast<unsigned char>(r / cnt);
      pOut[3] = static_cast<unsigned char>(a / cnt);
    }
  }

  cairo_surface_mark_dirty(dst);
  return dst;
}

struct LiveWallpaperLayout {
  int marginX = 32;
  int tabY = 24;
  int tabH = 48;
  int contentStartY = 148;
  int heroW = 0;
  int heroH = 220;
  int ctrlX = 0;
  int ctrlW = 0;
  int tab0W = 0;
  int tab1W = 0;
  int galleryY = 0;
  int galleryHeaderY = 0;
  int sortPillsY = 0;
  int galleryGridY = 0;
  int bottomReserve = 56;
  int gap = 24;
  int titleFont = 24;
  int tabFont = 16;
  int galleryFont = 13;
  int pillFont = 12;
  int fillCardH = 60;
  int openPickerH = 48;
  int actBtnH = 44;
  int heroNavSize = 36;
  int playBtnS = 56;
  int comboW = 110;
  int comboH = 28;
  int pillH = 28;
  int badgeFont = 11;
  double scale = 1.0;
};

static inline LiveWallpaperLayout compute_lw_layout(int windowWidth, int windowHeight) {
  LiveWallpaperLayout L;

  const double s = std::clamp(std::min(
      static_cast<double>(windowWidth) / 1920.0,
      static_cast<double>(windowHeight) / 1080.0), 0.4, 2.5);
  L.scale = s;

  L.marginX = static_cast<int>(32.0 * s);
  L.tabY = static_cast<int>(24.0 * s);
  L.tabH = static_cast<int>(48.0 * s);
  L.contentStartY = static_cast<int>(88.0 * s);
  const int ctrlGap = static_cast<int>(12.0 * s);
  L.heroH = L.fillCardH + ctrlGap + L.openPickerH + ctrlGap + L.actBtnH;
  L.gap = static_cast<int>(12.0 * s);
  L.bottomReserve = static_cast<int>(56.0 * s);
  L.titleFont = static_cast<int>(24.0 * s);
  L.tabFont = static_cast<int>(16.0 * s);
  L.galleryFont = static_cast<int>(13.0 * s);
  L.pillFont = static_cast<int>(12.0 * s);
  L.fillCardH = static_cast<int>(60.0 * s);
  L.openPickerH = static_cast<int>(48.0 * s);
  L.actBtnH = static_cast<int>(44.0 * s);
  L.heroNavSize = static_cast<int>(36.0 * s);
  L.playBtnS = static_cast<int>(56.0 * s);
  L.comboW = static_cast<int>(110.0 * s);
  L.comboH = static_cast<int>(28.0 * s);
  L.pillH = static_cast<int>(28.0 * s);
  L.badgeFont = static_cast<int>(11.0 * s);

  const int contentW = windowWidth - 2 * L.marginX;
  L.heroW = (contentW - L.gap) * 8 / 12;
  L.heroW = std::max(L.heroW, static_cast<int>(400.0 * s));
  L.ctrlW = contentW - L.gap - L.heroW;
  L.ctrlX = L.marginX + L.heroW + L.gap;

  const int kTabPad = static_cast<int>(32.0 * s);
  L.tab0W = lw_text_width("Wallpaper", static_cast<float>(L.tabFont), 500) + kTabPad;
  L.tab1W = lw_text_width("Gallery settings", static_cast<float>(L.tabFont), 500) + kTabPad;

  L.galleryY = L.contentStartY + L.heroH + L.gap;
  L.galleryHeaderY = L.galleryY;
  L.sortPillsY = L.galleryHeaderY + static_cast<int>(16.0 * s);
  L.galleryGridY = L.sortPillsY + L.pillH + static_cast<int>(12.0 * s);

  return L;
}

} // namespace eh::live_wallpaper
