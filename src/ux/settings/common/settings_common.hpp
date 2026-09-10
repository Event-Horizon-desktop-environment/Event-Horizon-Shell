#pragma once

#include <algorithm>
#include <array>
#include <cairo/cairo.h>
#include <cmath>
#include <cstdio>
#include <string>

#include "m3/core/primitives/box.hpp"
#include "m3/core/label.hpp"
#include "m3/controls/input/toggle.hpp"
#include "m3/controls/input/slider.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "desktop_shell/ui/dropdown/dropdown.hpp"

// Shared drawing utilities for settings tabs

struct App;

namespace Theme {
// M3 dark default (June 2026 contrast-optimised)
static constexpr double BgR = 0.102;
static constexpr double BgG = 0.075;
static constexpr double BgB = 0.188;
static constexpr double TextR = 1.0;
static constexpr double TextG = 1.0;
static constexpr double TextB = 1.0;
static constexpr double AccR = 0.769;
static constexpr double AccG = 0.659;
static constexpr double AccB = 0.941;
} // namespace Theme

static inline void settings_resolve_colors(const App& app,
                                            float& accent_r, float& accent_g, float& accent_b,
                                            float& text_r, float& text_g, float& text_b,
                                            float& surface_r, float& surface_g, float& surface_b,
                                            float& outline_r, float& outline_g, float& outline_b) {
  if (app.settings.colorThemeEnabled) {
    accent_r = app.settings.themePrimaryR;
    accent_g = app.settings.themePrimaryG;
    accent_b = app.settings.themePrimaryB;
    text_r = app.settings.themeOnSurfaceR;
    text_g = app.settings.themeOnSurfaceG;
    text_b = app.settings.themeOnSurfaceB;
    surface_r = app.settings.themeSurfaceR;
    surface_g = app.settings.themeSurfaceG;
    surface_b = app.settings.themeSurfaceB;
    outline_r = app.settings.themeOutlineR;
    outline_g = app.settings.themeOutlineG;
    outline_b = app.settings.themeOutlineB;
  } else if (app.settings.matugenThemingEnabled && app.settings.matugenPaletteOk) {
    accent_r = app.settings.matugenAccentR;
    accent_g = app.settings.matugenAccentG;
    accent_b = app.settings.matugenAccentB;
    text_r = app.settings.matugenTextR;
    text_g = app.settings.matugenTextG;
    text_b = app.settings.matugenTextB;
    surface_r = app.settings.matugenSurfaceR;
    surface_g = app.settings.matugenSurfaceG;
    surface_b = app.settings.matugenSurfaceB;
    outline_r = app.settings.matugenOutlineR;
    outline_g = app.settings.matugenOutlineG;
    outline_b = app.settings.matugenOutlineB;
  } else if (app.settings.horizonColorsNative && app.settings.horizonColorsPaletteOk) {
    accent_r = app.settings.hcAccentR;
    accent_g = app.settings.hcAccentG;
    accent_b = app.settings.hcAccentB;
    text_r = 1.0f;
    text_g = 1.0f;
    text_b = 1.0f;
    surface_r = app.settings.hcSurfaceR;
    surface_g = app.settings.hcSurfaceG;
    surface_b = app.settings.hcSurfaceB;
    outline_r = app.settings.hcOutlineR;
    outline_g = app.settings.hcOutlineG;
    outline_b = app.settings.hcOutlineB;
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

// Layout constants.
static constexpr int kSpacingXS  = 4;
static constexpr int kSpacingS   = 8;
static constexpr int kSpacingM   = 12;
static constexpr int kSpacingL   = 16;
static constexpr int kSpacingXL  = 24;

static constexpr int kContentTop = 16;

static constexpr int kCardPad    = kSpacingL;
static constexpr int kCardGap    = kSpacingM;
static constexpr double kCardRad = 14.0;

static constexpr int kAppearanceIconCardH = 240;
static constexpr int kWallpaperToggleBandH = 80;

static constexpr int kSidebarW          = 260;
static constexpr int kSidebarTabBaseY   = kContentTop + kSpacingM;
static constexpr int kSidebarTabPitchY  = 48;
static constexpr int kSidebarTabH       = 40;

static constexpr int kSettingsComboW = 200;
static constexpr int kSettingsComboH = 28;
static constexpr int kQtColorSchemeComboH = 38;
static constexpr int kTabBarH = 36;
static constexpr int kSettingsDdRowH = 26;

static constexpr int kSliderRowH    = 68;

// Source color helpers.
static inline void paint_src_bg(App& app, cairo_t* cr, double a) {
  if (app.drawChromeMatugen)
    cairo_set_source_rgba(cr, app.drawChrome.panelFillR * 0.35, app.drawChrome.panelFillG * 0.35, app.drawChrome.panelFillB * 0.35, a);
  else
    cairo_set_source_rgba(cr, Theme::BgR * 0.35, Theme::BgG * 0.35, Theme::BgB * 0.35, a);
}

static inline void paint_src_glass_hi(App& app, cairo_t* cr, double a) {
  if (app.drawChromeMatugen) {
    const double hr = 0.48 + 0.52 * app.drawChrome.outlineR;
    const double hg = 0.48 + 0.52 * app.drawChrome.outlineG;
    const double hb = 0.48 + 0.52 * app.drawChrome.outlineB;
    cairo_set_source_rgba(cr, hr, hg, hb, a);
  } else
    cairo_set_source_rgba(cr, 1, 1, 1, a);
}

static inline void paint_src_accent(App& app, cairo_t* cr, double a) {
  if (app.drawChromeMatugen)
    cairo_set_source_rgba(cr, app.drawChrome.accentR, app.drawChrome.accentG, app.drawChrome.accentB, a);
  else
    cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB, a);
}

static inline void paint_src_dim(App& app, cairo_t* cr, double a) {
  if (app.drawChromeMatugen)
    cairo_set_source_rgba(cr, app.drawChrome.drawerDimR, app.drawChrome.drawerDimG, app.drawChrome.drawerDimB, a);
  else
    cairo_set_source_rgba(cr, Theme::BgR * 0.85, Theme::BgG * 0.9, Theme::BgB * 0.95, a);
}

static inline void paint_src_header(App& app, cairo_t* cr, double a) {
  if (app.drawChromeMatugen) {
    const double r = 0.45 * app.drawChrome.dockFillR + 0.55 * app.drawChrome.panelFillR;
    const double g = 0.45 * app.drawChrome.dockFillG + 0.55 * app.drawChrome.panelFillG;
    const double b = 0.45 * app.drawChrome.dockFillB + 0.55 * app.drawChrome.panelFillB;
    cairo_set_source_rgba(cr, r, g, b, a);
  } else
    cairo_set_source_rgba(cr, Theme::BgR, Theme::BgG, Theme::BgB, a);
}

static inline void paint_src_knob(App& app, cairo_t* cr, double a) {
  if (app.drawChromeMatugen) {
    const double kr = 0.82 + 0.18 * app.drawChrome.outlineR;
    const double kg = 0.82 + 0.18 * app.drawChrome.outlineG;
    const double kb = 0.82 + 0.18 * app.drawChrome.outlineB;
    cairo_set_source_rgba(cr, kr, kg, kb, a);
  } else
    cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, a);
}

// Round rect helper.
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

// Card background.
static inline void settings_card(App& app, cairo_t* cr, double cx, double cy, double cw, double ch, double glassOv) {
  m3::Box box;
  float r, g, b;
  if (app.drawChromeMatugen) {
    r = static_cast<float>(app.drawChrome.panelFillR * 0.35);
    g = static_cast<float>(app.drawChrome.panelFillG * 0.35);
    b = static_cast<float>(app.drawChrome.panelFillB * 0.35);
  } else {
    r = static_cast<float>(Theme::BgR * 0.35);
    g = static_cast<float>(Theme::BgG * 0.35);
    b = static_cast<float>(Theme::BgB * 0.35);
  }
  box.setColor(r, g, b, static_cast<float>(0.78 * glassOv));
  box.setRadius(kCardRad);
  box.setGeometry(static_cast<float>(cx), static_cast<float>(cy),
                  static_cast<float>(cw), static_cast<float>(ch));
  box.setGlassy(true);
  box.paint(cr);
}

// Category label (small uppercase-style label).
static inline void settings_cat_label(cairo_t* cr, double x, double y, const char* text) {
  if (!text || !text[0]) return;
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(10.0f * PANGO_SCALE));
  pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text, -1);
  int pw, ph;
  pango_layout_get_pixel_size(layout, &pw, &ph);
  cairo_save(cr);
  cairo_translate(cr, x, y - static_cast<double>(ph) + 2.0);
  cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.42f);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  pango_font_description_free(desc);
  g_object_unref(layout);
}

// Group section header: bold, larger, with a subtle background.
static inline void settings_group_header(App& app, cairo_t* cr, double x, double y, double w, const char* text) {
  {
    m3::Box box;
    float r, g, b;
    if (app.drawChromeMatugen) {
      r = static_cast<float>(app.drawChrome.panelFillR);
      g = static_cast<float>(app.drawChrome.panelFillG);
      b = static_cast<float>(app.drawChrome.panelFillB);
    } else {
      r = static_cast<float>(Theme::BgR);
      g = static_cast<float>(Theme::BgG);
      b = static_cast<float>(Theme::BgB);
    }
    box.setColor(r, g, b, 0.10f);
    box.setRadius(6.0f);
    box.setGeometry(static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), 32.0f);
    box.setGlassy(true);
    box.paint(cr);
  }
  {
    if (text && text[0]) {
      auto* layout = pango_cairo_create_layout(cr);
      auto* desc = pango_font_description_new();
      pango_font_description_set_family(desc, "Inter");
      pango_font_description_set_size(desc, static_cast<int>(12.0f * PANGO_SCALE));
      pango_font_description_set_weight(desc, static_cast<PangoWeight>(700));
      pango_layout_set_font_description(layout, desc);
      pango_layout_set_text(layout, text, -1);
      int pw, ph;
      pango_layout_get_pixel_size(layout, &pw, &ph);
      cairo_save(cr);
      cairo_translate(cr, x + 12.0, y + 20.0 - static_cast<double>(ph) + 2.0);
      cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.60f);
      pango_cairo_show_layout(cr, layout);
      cairo_restore(cr);
      pango_font_description_free(desc);
      g_object_unref(layout);
    }
  }
}

// Group gap constant.
static constexpr int kGroupGap = 6;

// Setting label, optionally with a description.
static inline void settings_label(cairo_t* cr, double x, double titleY, const char* title, const char* desc,
                                   bool show_desc = true) {
  {
    m3::Label lbl;
    lbl.setText(title);
    lbl.setFontSize(15.0f);
    lbl.setFontWeight(400);
    lbl.setColor(static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                 static_cast<float>(Theme::TextB), 0.93f);
    float tw, th;
    lbl.measureExtents(tw, th);
    lbl.paintAt(cr, static_cast<float>(x), static_cast<float>(titleY) - th + 3.0f);
  }
  if (desc && desc[0] && show_desc) {
    m3::Label lbl;
    lbl.setText(desc);
    lbl.setFontSize(11.0f);
    lbl.setFontWeight(400);
    lbl.setColor(static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                 static_cast<float>(Theme::TextB), 0.46f);
    float tw, th;
    lbl.measureExtents(tw, th);
    lbl.paintAt(cr, static_cast<float>(x), static_cast<float>(titleY + 16) - th + 2.0f);
  }
}

// M3 text helper, replaces cairo_show_text.
// Adjusts y from baseline to top for m3::Label.
static inline void settings_show_text(cairo_t* cr, double x, double y, const char* text,
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

// Value pill badge.
static inline void draw_value_pill(cairo_t* cr, int cx, int cy, int cw, const char* text) {
  if (!text || !text[0]) return;
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(11.0f * PANGO_SCALE));
  pango_font_description_set_weight(desc, PANGO_WEIGHT_MEDIUM);
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text, -1);
  int tw, th;
  pango_layout_get_pixel_size(layout, &tw, &th);

  const int padX = 8;
  const int padY = 3;
  const int pillW = tw + padX * 2;
  const int pillH = th + padY * 2;
  const int pillX = cx + cw - kCardPad - pillW;
  const int pillY = cy + 24 - th / 2 - padY;

  cairo_save(cr);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_GOOD);
  {
    m3::Box box;
    box.setColor(static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                 static_cast<float>(Theme::TextB), 0.08f);
    box.setRadius(static_cast<float>(pillH / 2));
    box.setGeometry(static_cast<float>(pillX), static_cast<float>(pillY),
                    static_cast<float>(pillW), static_cast<float>(pillH));
    box.setGlassy(true);
    box.paint(cr);
  }
  cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.60f);
  cairo_translate(cr, pillX + padX, pillY + padY);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);

  pango_font_description_free(desc);
  g_object_unref(layout);
}

// Toggle switch.
struct SwGeom { int x, y, w, h; };

static inline SwGeom settings_toggle(App& app, cairo_t* cr, int cx, int cy, int cw,
                                       double cy_card, double ch_card,
                                       bool on_state, double trackMaterialAlpha) {
  (void)app;
  (void)cr;
  (void)cy;
  (void)trackMaterialAlpha;
  constexpr int swW = 52, swH = 26;
  const int swX = cx + cw - swW - kSpacingXL;
  const int swY = static_cast<int>(cy_card + (ch_card - swH) / 2.0);
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  m3::Toggle toggle;
  toggle.setGeometry(static_cast<float>(swX), static_cast<float>(swY),
                     static_cast<float>(swW), static_cast<float>(swH));
  toggle.setOn(on_state);
  toggle.setAccentColor(a_r, a_g, a_b);
  toggle.setSurfaceColor(s_r, s_g, s_b);
  toggle.setTextColor(t_r, t_g, t_b);
  toggle.setOutlineColor(o_r, o_g, o_b);
  toggle.setHovered(false);
  toggle.paint(cr);
  return {swX, swY, swW, swH};
}

// Slider.
static inline void settings_slider(App& app, cairo_t* cr, int trX, int trY, int trW,
                                    int v, int vmin, int vmax, double paintPointerYOffset,
                                    const char* val_override = nullptr,
                                    bool inactive = false, double drag_track_norm = -1.0) {
  m3::Slider sl;
  sl.setRange(static_cast<float>(vmin), static_cast<float>(vmax));
  const bool use_drag = !inactive && drag_track_norm >= 0.0;
  if (use_drag) {
    const float dv = static_cast<float>(vmax - vmin);
    sl.setValue(static_cast<float>(vmin) + static_cast<float>(drag_track_norm) * dv);
  } else {
    sl.setValue(static_cast<float>(v));
  }
  sl.setStep(0.0f);
  sl.setGeometry(static_cast<float>(trX), static_cast<float>(trY) - 10.0f, static_cast<float>(trW), 36.0f);
  sl.setEnabled(!inactive);
  const double py = app.pointerY + paintPointerYOffset;
  const bool hov = !inactive && app.pointerX >= trX - 6 && app.pointerX < trX + trW + 6 &&
                   py >= trY - 10 && py < trY + 36;
  sl.setHovered(hov);

  std::array<char, 32> vbuf;
  const char* disp = val_override;
  if (!disp) {
    std::snprintf(vbuf.data(), vbuf.size(), "%d px", v);
    disp = vbuf.data();
  }
  sl.setShowValueLabel(true);
  sl.setValueLabel(disp);
  if (use_drag) sl.setPressed(true);

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  sl.setAccentColor(a_r, a_g, a_b);
  sl.setSurfaceColor(s_r, s_g, s_b);
  sl.setTextColor(t_r, t_g, t_b);

  sl.paint(cr);
}

// Point-in-rect test.
static inline bool point_in_rect(double px, double py, int x, int y, int w, int h) {
  return px >= static_cast<double>(x) && px < static_cast<double>(x + w) &&
         py >= static_cast<double>(y) && py < static_cast<double>(y + h);
}

// Slider value from a pixel position.
static inline int slider_value_from_x(double px, int trX, int trW, int minV, int maxV) {
  if (trW <= 0) return minV;
  double t = (px - static_cast<double>(trX)) / static_cast<double>(trW);
  t = std::max(0.0, std::min(1.0, t));
  return minV + static_cast<int>(std::lround(t * static_cast<double>(maxV - minV)));
}

// Combo box (closed state).
static inline void settings_paint_combo_closed(App& app, cairo_t* cr, int bx, int by, int bw, int bh, double glassOv,
                                                const char* displayText, bool expanded, int logicalPointerYOffset = 0) {
  (void)expanded;
  const double pyL = app.pointerY + static_cast<double>(logicalPointerYOffset);
  const bool hovered =
      app.pointerX >= bx && pyL >= static_cast<double>(by) && app.pointerX < bx + bw && pyL < static_cast<double>(by + bh);
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  eh::ui::Dropdown dd;
  std::vector<std::string> opts = {displayText ? displayText : ""};
  dd.setOptions(std::move(opts));
  dd.setSelectedIndex(0);
  dd.setGeometry(static_cast<float>(bx), static_cast<float>(by), static_cast<float>(bw), static_cast<float>(bh));
  dd.setEnabled(true);
  dd.setAccentColor(a_r, a_g, a_b);
  dd.setSurfaceColor(s_r, s_g, s_b);
  dd.setTextColor(t_r, t_g, t_b);
  dd.setOutlineColor(o_r, o_g, o_b);
  dd.setTriggerHovered(hovered);
  dd.paintTrigger(cr);
  (void)glassOv;
}

