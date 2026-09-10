#include "desktop_shell/widgets/weather/weather_paint.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/widgets/shared/measure_scratch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

#include <cairo.h>
#include <pango/pangocairo.h>

namespace eh::widgets {
namespace {



struct WeatherTextCache {
  std::string tempStr;
  std::string unit;
  int tempW = 0;
  int tempH = 0;
  int unitW = 0;
  int unitH = 0;
  double lastFontPx = 0;
  PangoLayout* layout = nullptr;
  ~WeatherTextCache() { if (layout) g_object_unref(layout); }
  WeatherTextCache() = default;
  WeatherTextCache(WeatherTextCache&& o) noexcept
      : tempStr(std::move(o.tempStr)), unit(std::move(o.unit)),
        tempW(o.tempW), tempH(o.tempH), unitW(o.unitW), unitH(o.unitH),
        lastFontPx(o.lastFontPx), layout(o.layout) {
    o.layout = nullptr;
  }
  WeatherTextCache& operator=(WeatherTextCache&& o) noexcept {
    if (layout) g_object_unref(layout);
    tempStr = std::move(o.tempStr);
    unit = std::move(o.unit);
    tempW = o.tempW;
    tempH = o.tempH;
    unitW = o.unitW;
    unitH = o.unitH;
    lastFontPx = o.lastFontPx;
    layout = o.layout;
    o.layout = nullptr;
    return *this;
  }
  WeatherTextCache(const WeatherTextCache&) = delete;
  WeatherTextCache& operator=(const WeatherTextCache&) = delete;
};
static std::unordered_map<std::string, WeatherTextCache> g_weatherTextCache;

PangoLayout* make_layout(cairo_t* cr, const char* font_desc_str) {
   
  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = pango_font_description_from_string(font_desc_str);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
  pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  return layout;
}

std::string weather_temp_display(const ControlCenterWeatherState& ws) {
   
  if (!ws.available) return "--\xC2\xB0";
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%d\xC2\xB0", ws.temp);
  return std::string(buf);
}

void weather_layout_inner(const eh::config::ShellConfig& sc, std::string_view instance_id, double icon_ref_px,
                          double slot_h, const ControlCenterWeatherState& ws, double& out_icon_px, double& out_gap1,
                          double& out_temp_w, double& out_pill_w, double& out_pill_h) {
   
  const double fontPx = std::clamp(icon_ref_px * 0.38, 11.0, 15.0);
  out_icon_px = std::round(std::clamp(icon_ref_px * 0.42, 13.0, 18.0));
  out_gap1 = std::max(3.0, std::round(icon_ref_px * 0.09));
  const std::string tempStr = weather_temp_display(ws);
  const std::string unit = ws.fahrenheit ? "F" : "C";

  const std::string kid(instance_id);
  auto it = g_weatherTextCache.find(kid);
  if (it != g_weatherTextCache.end() && it->second.tempStr == tempStr && it->second.unit == unit) {
    out_temp_w = static_cast<double>(it->second.tempW);
    const double pillFont = std::max(8.0, std::round(fontPx * 0.72));
    out_pill_h = std::max(14.0, pillFont + 4.0);
    out_pill_w = static_cast<double>(it->second.unitW) + std::round(std::max(4.0, fontPx * 0.35)) * 2.0;
    (void)sc;
    (void)slot_h;
    return;
  }

  cairo_t* measure_cr = get_measure_cr();
  out_temp_w = 36.0;
  int tempH = 0;
  if (measure_cr) {
    std::string fd = "Inter Medium " + std::to_string(static_cast<int>(fontPx));
    PangoLayout* layout = make_layout(measure_cr, fd.c_str());
    pango_layout_set_text(layout, tempStr.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    g_object_unref(layout);
    out_temp_w = static_cast<double>(tw);
    tempH = th;
  }

  const double pillFont = std::max(8.0, std::round(fontPx * 0.72));
  out_pill_h = std::max(14.0, pillFont + 4.0);
  out_pill_w = 18.0;
  int unitH = 0;
  if (measure_cr) {
    std::string fd2 = "Inter Medium " + std::to_string(static_cast<int>(pillFont));
    PangoLayout* layout2 = make_layout(measure_cr, fd2.c_str());
    pango_layout_set_text(layout2, unit.c_str(), -1);
    int uw = 0, uh = 0;
    pango_layout_get_pixel_size(layout2, &uw, &uh);
    g_object_unref(layout2);
    out_pill_w = static_cast<double>(uw) + std::round(std::max(4.0, fontPx * 0.35)) * 2.0;
    unitH = uh;
  }

  if (measure_cr) {
    int rawTempW = static_cast<int>(out_temp_w);
    int rawUnitW = static_cast<int>(out_pill_w - std::round(std::max(4.0, fontPx * 0.35)) * 2.0);
    WeatherTextCache& entry = g_weatherTextCache[kid];
    entry.tempStr = tempStr;
    entry.unit = unit;
    entry.tempW = rawTempW;
    entry.tempH = tempH;
    entry.unitW = rawUnitW;
    entry.unitH = unitH;
  }

  (void)sc;
  (void)slot_h;
}

}

double dock_weather_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc, std::string_view instance_id,
                                double icon_ref_px, double bar_height) {
   
  (void)measure_cr;
  const ControlCenterWeatherState ws = control_center_weather_state(sc, instance_id);
  double iconPx{}, gap1{}, tempW{}, pillW{}, pillH{};
  weather_layout_inner(sc, instance_id, icon_ref_px, bar_height, ws, iconPx, gap1, tempW, pillW, pillH);
  (void)pillH;
  const double inner = iconPx + gap1 + tempW + gap1 + pillW;
  const double padX = 10.0;
  const double minW = std::max(icon_ref_px * 1.25, 72.0);
  const double maxCap = std::min(bar_height * 6.0, 220.0);
  return std::clamp(inner + padX * 2.0, minW, maxCap);
}

void paint_weather_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id, double x, double y,
                        double slot_w, double slot_h, double icon_ref_px, bool hovered, bool pressed) {
  (void)hovered; (void)pressed;
  const ControlCenterWeatherState ws = control_center_weather_state(sc, instance_id);
  double iconPx{}, gap1{}, tempW{}, pillW{}, pillH{};
  weather_layout_inner(sc, instance_id, icon_ref_px, slot_h, ws, iconPx, gap1, tempW, pillW, pillH);

  cairo_save(cr);
  slot_pill_style::paint_pill(cr, x, y, slot_w, slot_h);

  const double contentW = iconPx + gap1 + tempW + gap1 + pillW;
  const double padX = (slot_w - contentW) * 0.5;
  const double baseX = x + std::max(6.0, padX);
  const double cy = y + slot_h * 0.5;

  const char* glyph = (!ws.icon.empty()) ? ws.icon.c_str() : "cloud";
  const double gAlpha = ws.available ? 1.0 : 0.45;
  eh::shell::draw_material_glyph(cr, baseX + iconPx * 0.5, cy, iconPx, glyph, 1.0, 1.0, 1.0, gAlpha);

  const double fontPx = std::clamp(icon_ref_px * 0.38, 11.0, 15.0);
  std::string fd = "Inter Medium " + std::to_string(static_cast<int>(fontPx));
  const std::string wkid(instance_id);
  auto& wEntry = g_weatherTextCache[wkid];
  if (!wEntry.layout || wEntry.lastFontPx != fontPx) {
    if (wEntry.layout) g_object_unref(wEntry.layout);
    wEntry.layout = make_layout(cr, fd.c_str());
    wEntry.lastFontPx = fontPx;
  } else {
    pango_cairo_update_layout(cr, wEntry.layout);
  }
  PangoLayout* layout = wEntry.layout;
  const std::string tempStr = weather_temp_display(ws);
  pango_layout_set_text(layout, tempStr.c_str(), -1);
  pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
  int tw = 0, th = 0;
  if (wEntry.tempStr == tempStr && wEntry.unit == (ws.fahrenheit ? "F" : "C")) {
    tw = wEntry.tempW;
    th = wEntry.tempH;
  } else {
    pango_layout_get_pixel_size(layout, &tw, &th);
  }
  const double tx = baseX + iconPx + gap1;
  const double ty = y + (slot_h - static_cast<double>(th)) * 0.5;
  const double textAlpha = ws.available ? 1.0 : 0.55;
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, textAlpha);
  cairo_move_to(cr, tx, ty);
  pango_cairo_show_layout(cr, layout);

  const double pillX = tx + static_cast<double>(tw) + gap1;
  const double pillY = y + (slot_h - pillH) * 0.5;
  const double pillR = pillH * 0.5;
  cairo_new_path(cr);
  cairo_arc(cr, pillX + pillW - pillR, pillY + pillR, pillR, -M_PI_2, M_PI_2);
  cairo_arc(cr, pillX + pillR, pillY + pillR, pillR, M_PI_2, 3 * M_PI_2);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, 0.55, 0.80, 1.00, 0.16);
  cairo_fill(cr);

  const std::string unit = ws.fahrenheit ? "F" : "C";
  const double pillFont = std::max(8.0, std::round(fontPx * 0.72));
  std::string fdU = "Inter Medium " + std::to_string(static_cast<int>(pillFont));
  if (!wEntry.layout || wEntry.lastFontPx != pillFont) {
    if (wEntry.layout) g_object_unref(wEntry.layout);
    wEntry.layout = make_layout(cr, fdU.c_str());
    wEntry.lastFontPx = pillFont;
  } else {
    pango_cairo_update_layout(cr, wEntry.layout);
  }
  PangoLayout* layU = wEntry.layout;
  pango_layout_set_text(layU, unit.c_str(), -1);
  int uw = 0, uh = 0;
  if (wEntry.tempStr == tempStr && wEntry.unit == unit) {
    uw = wEntry.unitW;
    uh = wEntry.unitH;
  } else {
    pango_layout_get_pixel_size(layU, &uw, &uh);
  }
  cairo_set_source_rgba(cr, 0.55, 0.82, 1.00, 1.0);
  cairo_move_to(cr, pillX + (pillW - static_cast<double>(uw)) * 0.5, pillY + (pillH - static_cast<double>(uh)) * 0.5);
  pango_cairo_show_layout(cr, layU);

  cairo_restore(cr);
}

}
