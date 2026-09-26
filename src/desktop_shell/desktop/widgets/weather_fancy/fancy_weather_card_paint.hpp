#pragma once

// Shared fancy weather card, single source for the desktop weather-fancy
// widget and the dock/taskbar weather popup. Same code, same pixels — the two
// surfaces cannot drift apart. The vertical flow is computed once in
// `fancy_weather_flow()` and consumed by both the measure and the paint, so
// the popup can never size itself differently from the widget it mirrors.
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <pango/pango.h>
#include <pango/pangocairo.h>

namespace eh::shell::desktop {

namespace fancy_detail {

inline PangoLayout* make_pango(cairo_t* cr, const char* descStr) {
  PangoLayout* lay = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = pango_font_description_from_string(descStr);
  pango_layout_set_font_description(lay, desc);
  pango_font_description_free(desc);
  return lay;
}

inline void measure_into(PangoLayout* l, const std::string& s, int& w, int& h) {
  pango_layout_set_text(l, s.c_str(), -1);
  pango_layout_get_pixel_size(l, &w, &h);
}

struct FancyStrings {
  std::string city, temp, cond, hl;
  std::string feels, humid, wind, vis;
};

inline FancyStrings fancy_strings(const eh::shell::dock::control_center::ControlCenterWeatherState& ws) {
  FancyStrings s;
  const auto comma = ws.location.find(',');
  s.city = (comma == std::string::npos) ? ws.location : ws.location.substr(0, comma);
  char buf[32];
  if (ws.available) {
    std::snprintf(buf, sizeof(buf), "%d\u00b0", ws.temp);
    s.temp = buf;
    s.cond = ws.condition;
    std::snprintf(buf, sizeof(buf), "H:%d\u00b0  L:%d\u00b0", ws.hi, ws.lo);
    s.hl = buf;
    std::snprintf(buf, sizeof(buf), "%d\u00b0", ws.feels_like);
    s.feels = buf;
    std::snprintf(buf, sizeof(buf), "%d%%", ws.humidity_pct);
    s.humid = buf;
    std::snprintf(buf, sizeof(buf), "%d km/h", ws.wind_kmh);
    s.wind = buf;
    if (ws.visibility_m >= 1000)
      std::snprintf(buf, sizeof(buf), "%.0f km", ws.visibility_m / 1000.0);
    else
      std::snprintf(buf, sizeof(buf), "%d m", ws.visibility_m);
    s.vis = buf;
  } else {
    s.temp = "--";
    s.cond = ws.status_text;
    s.hl = "--";
    s.feels = s.humid = s.wind = s.vis = "--";
  }
  return s;
}

// One vertical pass over the card, driven by measured text heights.
struct FancyFlow {
  double cityY = 0, tempY = 0, condY = 0, hlY = 0;
  double chipsY = 0, foreTitleY = 0, colsY = 0;
  int tempH = 0;
  int totalH = 0;
};

inline constexpr double kPad = 20.0;
inline constexpr double kRadius = 26.0;
inline constexpr double kChipH = 66.0;
inline constexpr double kColH = 78.0;
inline constexpr double kChipGap = 8.0;
inline constexpr double kColGap = 8.0;
inline constexpr double kBottomPad = 16.0;

inline FancyFlow fancy_weather_flow(const FancyStrings& s) {
  // Scratch surface: measurement must not depend on the target context.
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
  cairo_t* cr = cairo_create(surf);

  PangoLayout* cityL = make_pango(cr, "Inter SemiBold 15");
  PangoLayout* tempL = make_pango(cr, "Inter Light 56");
  PangoLayout* condL = make_pango(cr, "Inter 14");
  PangoLayout* hlL = make_pango(cr, "Inter 13");
  PangoLayout* foreTitleL = make_pango(cr, "Inter SemiBold 12");

  int cw = 0, chh = 0, tw = 0, th = 0, cow = 0, coh = 0, hw = 0, hh = 0;
  measure_into(cityL, s.city, cw, chh);
  measure_into(tempL, s.temp, tw, th);
  measure_into(condL, s.cond, cow, coh);
  measure_into(hlL, s.hl, hw, hh);
  int titleW = 0, titleH = 0;
  measure_into(foreTitleL, "5-Day Forecast", titleW, titleH);

  FancyFlow f;
  f.tempH = th;
  double y = kPad;
  f.cityY = y;
  if (!s.city.empty()) y += chh + 2.0;
  f.tempY = y;
  y += th + 2.0;
  f.condY = y;
  y += coh + 2.0;
  f.hlY = y;
  y += hh + 12.0;
  f.chipsY = y;
  y += kChipH + 12.0;
  f.foreTitleY = y;
  y += titleH + 6.0;
  f.colsY = y;
  y += kColH;
  f.totalH = static_cast<int>(std::ceil(y + kBottomPad));

  g_object_unref(cityL);
  g_object_unref(tempL);
  g_object_unref(condL);
  g_object_unref(hlL);
  g_object_unref(foreTitleL);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return f;
}

} // namespace fancy_detail

// Card height for the current weather state. Popup hosts call this to size
// their layer before the first real paint.
inline int measure_fancy_weather_height(const eh::config::ShellConfig& sc,
                                        const std::string& widgetId) {
  const auto& ws =
      eh::shell::dock::control_center::control_center_weather_state(sc, widgetId);
  return fancy_detail::fancy_weather_flow(fancy_detail::fancy_strings(ws)).totalH;
}

// Paints the card at x=0 and returns the height it actually used, so hosts
// that own their surface height can grow to fit.
inline double paint_fancy_weather_card(cairo_t* cr, double W,
                                       const eh::config::ShellConfig& sc,
                                       const std::string& widgetId) {
  using eh::shell::shared::rounded_rect;
  namespace fd = fancy_detail;

  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const auto& ws =
      eh::shell::dock::control_center::control_center_weather_state(sc, widgetId);
  const double dimA = ws.available ? 1.0 : 0.45;
  const auto strs = fd::fancy_strings(ws);
  const auto flow = fd::fancy_weather_flow(strs);
  const double H = static_cast<double>(flow.totalH);

  PangoLayout* cityL = fd::make_pango(cr, "Inter SemiBold 15");
  PangoLayout* tempL = fd::make_pango(cr, "Inter Light 56");
  PangoLayout* condL = fd::make_pango(cr, "Inter 14");
  PangoLayout* hlL = fd::make_pango(cr, "Inter 13");
  PangoLayout* chipLabL = fd::make_pango(cr, "Inter 9");
  PangoLayout* chipValL = fd::make_pango(cr, "Inter SemiBold 13");
  PangoLayout* chipValSmL = fd::make_pango(cr, "Inter SemiBold 11");
  PangoLayout* foreTitleL = fd::make_pango(cr, "Inter SemiBold 12");
  PangoLayout* dayL = fd::make_pango(cr, "Inter SemiBold 11");
  PangoLayout* tmpL = fd::make_pango(cr, "Inter 11");

  auto show_at = [&](PangoLayout* l, double x, double y, double a) {
    cairo_move_to(cr, x, y);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, a * dimA);
    pango_cairo_show_layout(cr, l);
  };
  auto measure = [&](PangoLayout* l, const std::string& s, int& w, int& h) {
    fd::measure_into(l, s, w, h);
  };

  cairo_save(cr);

  // Dark glass card, single fill + single hairline so corners stay crisp.
  rounded_rect(cr, 0, 0, W, H, fd::kRadius);
  cairo_set_source_rgba(cr, mc.dockFillR * 0.30, mc.dockFillG * 0.30, mc.dockFillB * 0.30,
                        0.92 * sc.appearance.overlayOpacityWidgetCard);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // Hero text block.
  if (!strs.city.empty()) {
    pango_layout_set_text(cityL, strs.city.c_str(), -1);
    show_at(cityL, fd::kPad, flow.cityY, 0.78);
  }
  pango_layout_set_text(tempL, strs.temp.c_str(), -1);
  show_at(tempL, fd::kPad, flow.tempY, 0.96);
  pango_layout_set_text(condL, strs.cond.c_str(), -1);
  show_at(condL, fd::kPad, flow.condY, 0.70);
  pango_layout_set_text(hlL, strs.hl.c_str(), -1);
  show_at(hlL, fd::kPad, flow.hlY, 0.70);

  // Hero glyph right, centered on the temp block, soft halo.
  const char* heroIcon = (ws.available && !ws.icon.empty()) ? ws.icon.c_str() : "cloud_off";
  const double heroCx = W - fd::kPad - 30.0;
  const double heroCy = flow.tempY + flow.tempH * 0.5;
  cairo_new_path(cr);
  cairo_arc(cr, heroCx, heroCy, 34.0, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.14 * dimA);
  cairo_fill(cr);
  eh::shell::draw_material_glyph(cr, heroCx, heroCy, 52.0, heroIcon,
                                  mc.accentR, mc.accentG, mc.accentB, 0.92 * dimA);

  // Stat chips: Feels / Humidity / Wind / Visibility.
  const char* glyphs[4] = {"thermostat", "humidity_percentage", "air", "visibility"};
  const char* labels[4] = {"Feels Like", "Humidity", "Wind", "Visibility"};
  const std::string values[4] = {strs.feels, strs.humid, strs.wind, strs.vis};
  const double chipW = (W - fd::kPad * 2.0 - fd::kChipGap * 3.0) / 4.0;
  for (int i = 0; i < 4; ++i) {
    const double sx = fd::kPad + i * (chipW + fd::kChipGap);
    rounded_rect(cr, sx, flow.chipsY, chipW, fd::kChipH, 14.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    int lw = 0, lh = 0, vw = 0, vh = 0;
    measure(chipLabL, labels[i], lw, lh);
    measure(chipValL, values[i], vw, vh);
    // Shrink-to-fit so long values ("12 km/h", "42 km") never crowd the edge.
    PangoLayout* valL = chipValL;
    if (static_cast<double>(vw) > chipW - 16.0) {
      measure(chipValSmL, values[i], vw, vh);
      valL = chipValSmL;
    }
    // Center the icon/label/value block vertically from measured heights so
    // nothing hugs the top or bottom edge.
    constexpr double kChipIconH = 15.0;
    const double blockH = kChipIconH + 6.0 + lh + 3.0 + vh;
    const double blockY = flow.chipsY + (fd::kChipH - blockH) * 0.5;
    const double labelY = blockY + kChipIconH + 6.0;
    const double valueY = labelY + lh + 3.0;
    eh::shell::draw_material_glyph(cr, sx + chipW * 0.5, blockY + kChipIconH * 0.5, 15.0,
                                    glyphs[i], mc.accentR, mc.accentG, mc.accentB,
                                    0.85 * dimA);
    pango_layout_set_text(chipLabL, labels[i], -1);
    show_at(chipLabL, sx + (chipW - lw) * 0.5, labelY, 0.60);
    pango_layout_set_text(valL, values[i].c_str(), -1);
    cairo_move_to(cr, sx + (chipW - vw) * 0.5, valueY);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.92 * dimA);
    pango_cairo_show_layout(cr, valL);
  }

  // 5-day forecast columns.
  pango_layout_set_text(foreTitleL, "5-Day Forecast", -1);
  show_at(foreTitleL, fd::kPad, flow.foreTitleY, 0.80);
  const double colW = (W - fd::kPad * 2.0 - fd::kColGap * 4.0) / 5.0;
  const int nDays = std::min(5, static_cast<int>(ws.forecast.size()));
  for (int i = 0; i < 5; ++i) {
    const double cx = fd::kPad + i * (colW + fd::kColGap);
    const bool today = (i == 0) && (nDays > 0);
    rounded_rect(cr, cx, flow.colsY, colW, fd::kColH, 14.0);
    if (today)
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.22 * dimA + 0.05);
    else
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
    cairo_fill_preserve(cr);
    if (today)
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.45);
    else
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    if (i < nDays) {
      const auto& d = ws.forecast[static_cast<size_t>(i)];
      int dw = 0, dh = 0;
      measure(dayL, d.day, dw, dh);
      pango_layout_set_text(dayL, d.day.c_str(), -1);
      show_at(dayL, cx + (colW - dw) * 0.5, flow.colsY + 8.0, today ? 0.95 : 0.75);
      eh::shell::draw_material_glyph(cr, cx + colW * 0.5, flow.colsY + 38.0, 19.0,
                                      d.icon.c_str(), mc.accentR, mc.accentG, mc.accentB,
                                      0.90 * dimA);
      char mm[24];
      std::snprintf(mm, sizeof(mm), "%d\u00b0/%d\u00b0", d.hi, d.lo);
      int mw = 0, mh = 0;
      measure(tmpL, mm, mw, mh);
      pango_layout_set_text(tmpL, mm, -1);
      show_at(tmpL, cx + (colW - mw) * 0.5, flow.colsY + fd::kColH - mh - 8.0, 0.85);
    }
  }

  g_object_unref(cityL);
  g_object_unref(tempL);
  g_object_unref(condL);
  g_object_unref(hlL);
  g_object_unref(chipLabL);
  g_object_unref(chipValL);
  g_object_unref(chipValSmL);
  g_object_unref(foreTitleL);
  g_object_unref(dayL);
  g_object_unref(tmpL);
  cairo_restore(cr);
  return H;
}

} // namespace eh::shell::desktop
