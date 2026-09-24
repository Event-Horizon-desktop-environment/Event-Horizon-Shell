#include "desktop_shell/dashboard/dashboard_paint.hpp"

#include "desktop_shell/controlcenter/layout/control_center_layout.hpp"
#include "desktop_shell/controlcenter/layout/control_center_slider_metrics.hpp"
#include "desktop_shell/controlcenter/mixer/mixer_stream_icon_resolve.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint_utils.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/dashboard/dashboard_layout.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/spotlight/paint/spotlight_paint.hpp"
#include "desktop_shell/ui/slider/ui_slider.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "configuration/shell_config.hpp"

#include <wayland-client.h>

#include <algorithm>
#include <cairo/cairo.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace eh::shell::dashboard {
namespace {

namespace cc = eh::shell::dock::control_center;
namespace ccu = eh::shell::dock::control_center::paint_utils;
namespace sm = eh::shell::cc_slider;
namespace hooks = eh::shell::dock_slot_hooks;

constexpr double kCardR = 20.0;  // CC inner-card radius (control_center_popup_paint card lambda)
constexpr double kPanelR = 24.0;  // CC outer-popup radius (floating panel, same nesting)
constexpr double kNetHeaderH = 40.0;
constexpr double kMixerHeaderH = sm::kMixerHeaderH;
constexpr double kMixerRowH = sm::kMixerRowH;
constexpr double kSysHeaderH = 36.0;
constexpr double kSysRowH = 46.0;

const char* kMonthNames[12] = {"January", "February",   "March",    "April",  "May",      "June",
                               "July",    "August",     "September", "October", "November", "December"};
const char* kDowShort[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

int days_in_month(int y, int m) {
  static const int k[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 30;
  if (m == 2) {
    const bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    return leap ? 29 : 28;
  }
  return k[m - 1];
}

void rrect(cairo_t* cr, double x, double y, double w, double h, double r) { ccu::rrect(cr, x, y, w, h, r); }

void hover_hl(cairo_t* cr, const eh::config::ChromePaintColors& mc, double x, double y, double w, double h, double r) {
  rrect(cr, x, y, w, h, r);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.18);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.35);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
}

void row_bg(cairo_t* cr, double x, double y, double w, double h, double r) {
  rrect(cr, x, y, w, h, r);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
  cairo_fill(cr);
}

void selected_bg(cairo_t* cr, const eh::config::ChromePaintColors& mc, double x, double y, double w, double h,
                 double r) {
  rrect(cr, x, y, w, h, r);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.16);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.40);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);
}

std::string trunc(cairo_t* cr, const std::string& s, double maxW, double size) {
  cairo_set_font_size(cr, size);
  cairo_text_extents_t ex{};
  cairo_text_extents(cr, s.c_str(), &ex);
  if (ex.x_advance <= maxW) return s;
  // Sizing runs inside so callers can't measure at a stale font size; the
  // ellipsis guarantees a cut is visible instead of a mid-glyph hard clip,
  // and the budget may shrink to nothing (never a 3-char overflowing stub).
  static const std::string kEll = "\u2026";
  cairo_text_extents(cr, kEll.c_str(), &ex);
  if (ex.x_advance > maxW) return {};
  std::string out = s;
  while (!out.empty()) {
    eh::shell::str::utf8_pop_back(out);
    const std::string cand = out + kEll;
    cairo_text_extents(cr, cand.c_str(), &ex);
    if (ex.x_advance <= maxW) return cand;
  }
  return kEll;
}

void text_c(cairo_t* cr, const std::string& s, double cx, double baseline, double size, double r, double g, double b,
            double a) {
  cairo_set_font_size(cr, size);
  cairo_text_extents_t te;
  cairo_text_extents(cr, s.c_str(), &te);
  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_move_to(cr, cx - te.x_advance * 0.5, baseline);
  cairo_show_text(cr, s.c_str());
}

void text_l(cairo_t* cr, const std::string& s, double x, double baseline, double size, double r, double g, double b,
            double a) {
  cairo_set_font_size(cr, size);
  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_move_to(cr, x, baseline);
  cairo_show_text(cr, s.c_str());
}

void text_r(cairo_t* cr, const std::string& s, double rightX, double baseline, double size, double r, double g,
            double b, double a) {
  cairo_set_font_size(cr, size);
  cairo_text_extents_t te;
  cairo_text_extents(cr, s.c_str(), &te);
  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_move_to(cr, rightX - te.x_advance, baseline);
  cairo_show_text(cr, s.c_str());
}

void card_title(cairo_t* cr, const eh::config::ChromePaintColors& mc, const CardRect& c, const char* glyph,
                const char* label) {
  eh::shell::draw_material_glyph(cr, c.x + 24.0, c.y + kSysHeaderH * 0.5 + 1.0, 18.0, glyph, mc.textR, mc.textG,
                                 mc.textB, 1.0);
  text_l(cr, label, c.x + 40.0, c.y + kSysHeaderH * 0.5 + 5.0, 15.0, mc.textR, mc.textG, mc.textB, 1.0);
}

// ---------------------------------------------------------------- clock ----

void paint_clock(DockApp& app, cairo_t* cr, const CardRect& c, double s) {
  (void)app;
  const auto mc = eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  ccu::cc_paint_glass_card_mc(cr, c.x, c.y, c.w, c.h, kCardR, s, mc);

  const DashClockText t = dashboard_clock_text(eh::config::shell_config_snapshot());

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.95);
  cairo_arc(cr, c.x + 20.0, c.y + 24.0, 4.0, 0.0, 2.0 * M_PI);
  cairo_fill(cr);
  text_l(cr, t.zone.empty() ? std::string("Local time") : t.zone, c.x + 32.0, c.y + 28.0, 11.0, mc.textR, mc.textG, mc.textB,
         0.90);

  text_c(cr, t.time, c.x + c.w * 0.5, c.y + 92.0, 44.0, mc.textR, mc.textG, mc.textB, 1.0);
  if (!t.date.empty())
    text_c(cr, t.date, c.x + c.w * 0.5, c.y + 118.0, 13.0, mc.textR, mc.textG, mc.textB, 0.95);
}

// ------------------------------------------------------------- calendar ----

void paint_calendar(DockApp& app, cairo_t* cr, const CardRect& c, double s) {
  auto& d = app.dash;
  const auto mc = eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  ccu::cc_paint_glass_card_mc(cr, c.x, c.y, c.w, c.h, kCardR, s, mc);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  // Title: bright month + dim year, truncated to clear the Today pill.
  const std::string month =
      trunc(cr, kMonthNames[std::clamp(d.calMonth, 1, 12) - 1], std::max(40.0, c.w - 230.0), 15.0);
  const std::string year = std::string(" ") + std::to_string(d.calYear);
  cairo_set_font_size(cr, 15.0);
  cairo_text_extents_t mte;
  cairo_text_extents(cr, month.c_str(), &mte);
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 1.0);
  cairo_move_to(cr, c.x + 16.0, c.y + 28.0);
  cairo_show_text(cr, month.c_str());
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.85);
  cairo_move_to(cr, c.x + 16.0 + mte.x_advance, c.y + 28.0);
  cairo_show_text(cr, year.c_str());

  // Prev / next chevrons: subtle squares matching their 28x28 hit rects.
  for (int i = 0; i < 2; ++i) {
    const double cx = c.x + c.w - (i == 0 ? 60.0 : 28.0);
    const double cy = c.y + 22.0;
    rrect(cr, cx - 14.0, cy - 14.0, 28.0, 28.0, 8.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.06);
    cairo_fill(cr);
    eh::shell::draw_material_glyph(cr, cx, cy + 1.0, 18.0, i == 0 ? "chevron_left" : "chevron_right", 0.88, 0.93,
                                   0.96, 1.0);
  }

  // Today pill: jumps back to the current month.
  rrect(cr, c.x + c.w - 146.0, c.y + 8.0, 64.0, 28.0, 14.0);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
  cairo_fill(cr);
  text_c(cr, "Today", c.x + c.w - 146.0 + 32.0, c.y + 26.0, 11.0, mc.textR, mc.textG, mc.textB, 0.95);

  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.22);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, c.x + 14.0, c.y + 44.0);
  cairo_line_to(cr, c.x + c.w - 14.0, c.y + 44.0);
  cairo_stroke(cr);

  constexpr double kInset = 16.0;
  const double gridW = std::max(40.0, c.w - 2.0 * kInset);
  const double colW = gridW / 7.0;
  const double gridTop = c.y + kDashCalHeaderH + kDashCalDowH;

  for (int i = 0; i < 7; ++i)
    text_l(cr, kDowShort[i], c.x + kInset + static_cast<double>(i) * colW + 8.0, c.y + kDashCalHeaderH + 18.0, 10.0,
           mc.textR, mc.textG, mc.textB, 0.80);

  const int y = d.calYear, m = std::clamp(d.calMonth, 1, 12);
  const auto ymd = std::chrono::year_month_day{std::chrono::year{y} / std::chrono::month{static_cast<unsigned>(m)} /
                                               std::chrono::day{1}};
  const unsigned firstDow =
      static_cast<unsigned>(std::chrono::weekday{static_cast<std::chrono::sys_days>(ymd)}.c_encoding());
  const int dim = days_in_month(y, m);
  const int prevDim = days_in_month(m == 1 ? y : y, m == 1 ? 12 : m - 1);

  const auto today = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(
      std::chrono::system_clock::now())};
  const int todayY = static_cast<int>(today.year());
  const int todayM = static_cast<int>(static_cast<unsigned>(today.month()));
  const int todayD = static_cast<int>(static_cast<unsigned>(today.day()));

  // Hairline cell separators.
  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.10);
  cairo_set_line_width(cr, 1.0);
  for (int i = 0; i <= 7; ++i) {
    const double lx = c.x + kInset + static_cast<double>(i) * colW;
    cairo_move_to(cr, lx, gridTop);
    cairo_line_to(cr, lx, gridTop + 6.0 * kDashCalRowH);
  }
  for (int r = 0; r <= 6; ++r) {
    const double ly = gridTop + static_cast<double>(r) * kDashCalRowH;
    cairo_move_to(cr, c.x + kInset, ly);
    cairo_line_to(cr, c.x + kInset + gridW, ly);
  }
  cairo_stroke(cr);

  int day = 1 - static_cast<int>(firstDow);
  for (int r = 0; r < 6; ++r) {
    for (int col = 0; col < 7; ++col, ++day) {
      const double cellX = c.x + kInset + static_cast<double>(col) * colW;
      const double cellTop = gridTop + static_cast<double>(r) * kDashCalRowH;
      int show;
      double alpha;
      if (day < 1) {
        show = prevDim + day;
        alpha = 0.32;
      } else if (day > dim) {
        show = day - dim;
        alpha = 0.32;
      } else {
        show = day;
        alpha = 0.90;
      }
      const bool isToday =
          (day >= 1 && day <= dim) && (y == todayY) && (m == todayM) && (day == todayD);
      if (isToday) {
        rrect(cr, cellX + 3.0, cellTop + 2.0, 27.0, 23.0, 7.0);
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.85);
        cairo_fill(cr);
        text_c(cr, std::to_string(show), cellX + 16.5, cellTop + 18.0, 12.0, 0.05, 0.05, 0.07, 1.0);
      } else {
        text_l(cr, std::to_string(show), cellX + 8.0, cellTop + 18.0, 12.0, mc.textR, mc.textG, mc.textB, alpha);
      }
    }
  }
}

// -------------------------------------------------------------- weather ----

void paint_weather(DockApp& app, cairo_t* cr, const CardRect& c, double s) {
  const auto mc = eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  ccu::cc_paint_glass_card_mc(cr, c.x, c.y, c.w, c.h, kCardR, s, mc);

  const auto ws = hooks::control_center_weather_state(eh::config::shell_config_snapshot(),
                                                      dashboard_weather_instance_id(app));
  const char unitCh = ws.fahrenheit ? 'F' : 'C';
  const char* glyph = ws.available ? (ws.icon.empty() ? "cloud" : ws.icon.c_str()) : "cloud_off";

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  eh::shell::draw_material_glyph(cr, c.x + 34.0, c.y + 44.0, 36.0, glyph, 0.94, 0.96, 0.98, 1.0);
  text_l(cr, ws.available ? (std::to_string(ws.temp) + "\u00b0" + unitCh) : "--", c.x + 58.0, c.y + 58.0, 42.0,
         mc.textR, mc.textG, mc.textB, 1.0);

  std::string city = ws.location;
  const auto p = city.find(',');
  if (p != std::string::npos) city = city.substr(0, p);
  text_l(cr, trunc(cr, city.empty() ? std::string("No location") : city, c.w - 36.0, 13.0), c.x + 18.0, c.y + 104.0, 13.0,
         mc.textR, mc.textG, mc.textB, 1.0);
  text_l(cr, ws.fahrenheit ? std::string("Fahrenheit") : std::string("Celsius"), c.x + 18.0, c.y + 124.0, 12.0,
         mc.textR, mc.textG, mc.textB, 0.92);

  const double chipY = c.y + 134.0;
  const double chipH = 42.0;
  const double chipGap = 8.0;
  const double chipW = std::max(30.0, (c.w - 36.0 - 3.0 * chipGap) / 4.0);
  auto chip = [&](int i, const char* label, const std::string& val) {
    const double x = c.x + 18.0 + static_cast<double>(i) * (chipW + chipGap);
    rrect(cr, x, chipY, chipW, chipH, 12.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.06);
    cairo_fill(cr);
    text_l(cr, trunc(cr, label, chipW - 16.0, 10.0), x + 10.0, chipY + 16.0, 10.0, mc.textR, mc.textG, mc.textB, 0.90);
    text_l(cr, trunc(cr, val, chipW - 16.0, 13.0), x + 10.0, chipY + 34.0, 13.0, mc.textR, mc.textG, mc.textB, 1.0);
  };
  chip(0, "Feels", ws.available ? (std::to_string(ws.feels_like) + "\u00b0" + unitCh) : "--");
  chip(1, "Humidity", ws.available ? (std::to_string(ws.humidity_pct) + "%") : "--");
  chip(2, "Wind", ws.available ? (std::to_string(ws.wind_kmh) + " km/h") : "--");
  chip(3, "Visibility", ws.available ? std::to_string(ws.visibility_m) : "--");

  if (!ws.forecast.empty()) {
    text_l(cr, "5-Day Forecast", c.x + 18.0, c.y + 196.0, 12.0, mc.textR, mc.textG, mc.textB, 0.92);
    const double fy0 = c.y + 206.0;
    const double fh = 64.0;
    const double fGap = 8.0;
    const double fw = std::max(24.0, (c.w - 36.0 - 4.0 * fGap) / 5.0);
    const int nf = std::min(5, static_cast<int>(ws.forecast.size()));
    for (int i = 0; i < nf; ++i) {
      const auto& fd = ws.forecast[static_cast<size_t>(i)];
      const double fx = c.x + 18.0 + static_cast<double>(i) * (fw + fGap);
      rrect(cr, fx, fy0, fw, fh, 10.0);
      cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.06);
      cairo_fill(cr);
      text_c(cr, trunc(cr, fd.day, std::max(8.0, fw - 8.0), 11.0), fx + fw * 0.5, fy0 + 16.0, 11.0, mc.textR, mc.textG, mc.textB,
             0.92);
      eh::shell::draw_material_glyph(cr, fx + fw * 0.5, fy0 + 36.0, 16.0, fd.icon.c_str(), 0.88, 0.93, 0.96, 1.0);
      text_c(cr, trunc(cr, std::to_string(fd.hi) + "\u00b0/" + std::to_string(fd.lo) + "\u00b0",
                       std::max(8.0, fw - 8.0), 12.0),
             fx + fw * 0.5, fy0 + 58.0, 12.0, mc.textR, mc.textG, mc.textB, 1.0);
    }
  }
}

// ---------------------------------------------------------------- media ----

void paint_media(DockApp& app, cairo_t* cr, const CardRect& c, double s) {
  auto& d = app.dash;
  const auto mc = eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  ccu::cc_paint_glass_card_mc(cr, c.x, c.y, c.w, c.h, kCardR, s, mc);

  eh::mpris::PlayerSnapshot ms{};
  if (app.mpris) ms = app.mpris->snapshot();
  const bool active = ms.active && (!ms.title.empty() || !ms.artist.empty());
  const bool playing = ms.playback_status == "Playing";

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  const double artX = c.x + 12.0, artY = c.y + 14.0, artS = 56.0;
  rrect(cr, artX, artY, artS, artS, 12.0);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
  cairo_fill(cr);
  if (active && ms.art) {
    cairo_save(cr);
    rrect(cr, artX, artY, artS, artS, 12.0);
    cairo_clip(cr);
    const int iw = cairo_image_surface_get_width(ms.art.get());
    const int ih = cairo_image_surface_get_height(ms.art.get());
    const double sc = artS / std::max(1, std::max(iw, ih));
    cairo_translate(cr, artX + (artS - iw * sc) * 0.5, artY + (artS - ih * sc) * 0.5);
    cairo_scale(cr, sc, sc);
    cairo_set_source_surface(cr, ms.art.get(), 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
  } else {
    eh::shell::draw_material_glyph(cr, artX + artS * 0.5, artY + artS * 0.55, 24.0, "music_note", 0.88, 0.93, 0.96, 1.0);
  }

  const double textX = c.x + 80.0;
  const double textMaxW = std::max(20.0, c.w - textX - 14.0);
  text_l(cr, trunc(cr, active ? (ms.title.empty() ? std::string("Unknown title") : ms.title) : std::string("No Media"),
                    textMaxW, 13.0),
         textX, c.y + 34.0, 13.0, mc.textR, mc.textG, mc.textB, 1.0);
  text_l(cr, trunc(cr, active ? (ms.artist.empty() ? std::string("Unknown artist") : ms.artist) : std::string("—"),
                    textMaxW, 12.0),
         textX, c.y + 54.0, 12.0, mc.textR, mc.textG, mc.textB, 0.92);

  const double cy = cc::cc_media_btn_cy(c.y, c.h);
  auto btn = [&](int idx, const char* glyph, bool enabled, bool primary) {
    const double cx = cc::cc_media_btn_cx(c.x, c.w, idx);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, 14.0, 0.0, 2.0 * M_PI);
    if (primary) {
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, enabled ? 0.85 : 0.30);
      cairo_fill(cr);
      eh::shell::draw_material_glyph(cr, cx, cy + 0.5, 18.0, glyph, 0.05, 0.05, 0.07, 1.0);
      return;
    }
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, enabled ? 0.10 : 0.04);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.13);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    eh::shell::draw_material_glyph(cr, cx, cy + 0.5, 18.0, glyph, 0.88, 0.93, 0.96, 1.0);
  };
  if (active) {
    btn(0, "skip_previous", ms.can_go_previous, false);
    btn(1, playing ? "pause" : "play_arrow", ms.can_play || ms.can_pause, true);
    btn(2, "skip_next", ms.can_go_next, false);
  }

  if (active && ms.duration_us > 0) {
    const DashMediaProgressGeom g = dashboard_media_progress_geom(c);
    double frac = std::clamp(static_cast<double>(ms.position_us) / static_cast<double>(ms.duration_us), 0.0, 1.0);
    if (d.dragging && d.dragKind == DashDragKind::Seek && d.dragVisualT >= 0.0)
      frac = std::clamp(d.dragVisualT, 0.0, 1.0);
    rrect(cr, g.trackX, g.trackY, g.trackW, g.trackH, 3.0);
    cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.85 * s + 0.15);
    cairo_fill(cr);
    const double fillW = g.trackW * frac;
    if (fillW > 0.5) {
      rrect(cr, g.trackX, g.trackY, std::max(6.0, fillW), g.trackH, 3.0);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.90);
      cairo_fill(cr);
      cairo_arc(cr, g.trackX + fillW, g.trackY + g.trackH * 0.5, 4.5, 0.0, 2.0 * M_PI);
      cairo_fill(cr);
    }
    const auto fmt = [](std::int64_t us) {
      const long total = static_cast<long>(us / 1000000);
      char b[32];
      std::snprintf(b, sizeof(b), "%ld:%02ld", total / 60, total % 60);
      return std::string(b);
    };
    text_l(cr, fmt(static_cast<std::int64_t>(frac * static_cast<double>(ms.duration_us))), c.x + 14.0, g.labelBase,
           11.0, mc.textR, mc.textG, mc.textB, 0.90);
    text_r(cr, fmt(ms.duration_us), c.x + c.w - 14.0, g.labelBase, 11.0, mc.textR, mc.textG, mc.textB, 0.90);
  }
}

// ---------------------------------------------------------------- mixer ----

void paint_mixer(DockApp& app, cairo_t* cr, const CardRect& c, double s) {
  auto& d = app.dash;
  const auto mc = eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  ccu::cc_paint_glass_card_mc(cr, c.x, c.y, c.w, c.h, kCardR, s, mc);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  const auto streams = hooks::control_center_mixer_streams();
  const int total = static_cast<int>(streams.size());
  const int n = std::min(total, c.span >= 2 ? 6 : 3);
  const DashMixerGeom g = dashboard_mixer_geom(c.x, c.w);

  eh::shell::draw_material_glyph(cr, c.x + 24.0, c.y + kMixerHeaderH * 0.5 + 1.0, 18.0,
                                 total > (c.span >= 2 ? 6 : 3) ? "expand_more" : "tune", 0.88, 0.93, 0.96, 1.0);
  text_l(cr, "Volume Mixer", g.nameX, c.y + kMixerHeaderH * 0.5 + 5.0, 15.0, mc.textR, mc.textG, mc.textB, 1.0);

  for (int i = 0; i < n; ++i) {
    const auto& st = streams[static_cast<size_t>(i)];
    const double rowY = c.y + kMixerHeaderH + static_cast<double>(i) * kMixerRowH;
    if (i > 0) {
      cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.12);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, c.x + 16.0, rowY + 0.5);
      cairo_line_to(cr, c.x + c.w - 16.0, rowY + 0.5);
      cairo_stroke(cr);
    }

    eh::shell::mixer_icon::StreamIconIds ids;
    ids.icon_name = st.icon_name;
    ids.app_id = st.app_id;
    ids.process_binary = st.process_binary;
    ids.process_path = st.process_path;
    ids.app_name = st.app_name;
    const eh::icons::IconEntry* icon = eh::shell::mixer_icon::resolve_mixer_stream_theme_icon(app.icons, ids);
    if (icon && icon->surface) {
      cairo_save(cr);
      cairo_translate(cr, c.x + 16.0, rowY + 15.0);
      cairo_scale(cr, 16.0 / std::max(1, icon->width), 16.0 / std::max(1, icon->height));
      cairo_set_source_surface(cr, icon->surface, 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);
    } else {
      cairo_arc(cr, c.x + 24.0, rowY + 23.0, 6.0, 0.0, 2.0 * M_PI);
      cairo_set_source_rgba(cr, 0.72, 0.76, 0.80, 0.92);
      cairo_fill(cr);
    }

    text_l(cr, trunc(cr, st.app_name.empty() ? st.description : st.app_name, g.nameBudget, 12.0), g.nameX, rowY + 27.0, 12.0,
           mc.textR, mc.textG, mc.textB, 0.95);

    double tDraw = std::clamp(static_cast<double>(st.volume_pct) / 100.0, 0.0, 1.0);
    if (d.dragging && d.dragKind == DashDragKind::Mixer && d.dragStreamId == st.sink_input_id &&
        d.dragVisualT >= 0.0)
      tDraw = std::clamp(d.dragVisualT, 0.0, 1.0);

    eh::ui::Slider sl;
    sl.setRange(0.0f, 1.0f);
    sl.setStep(0.01f);
    sl.setValue(static_cast<float>(tDraw));
    sl.setGeometry(static_cast<float>(g.trackX), static_cast<float>(rowY + sm::kMixerSliderCY - 19.0),
                   static_cast<float>(g.trackW), 38.0f);
    sl.setAccentColor(static_cast<float>(mc.accentR), static_cast<float>(mc.accentG),
                      static_cast<float>(mc.accentB));
    sl.setTrackColor(static_cast<float>(mc.drawerDimR), static_cast<float>(mc.drawerDimG),
                     static_cast<float>(mc.drawerDimB));
    sl.paint(cr);
  }

  if (n == 0)
    text_c(cr, "Nothing is playing", c.x + c.w * 0.5, c.y + kMixerHeaderH + 34.0, 12.0, mc.textR, mc.textG, mc.textB, 0.85);
}

// --------------------------------------------------------------- system ----

void paint_system(DockApp& app, cairo_t* cr, const CardRect& c, double s) {
  auto& d = app.dash;
  const auto mc = eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  ccu::cc_paint_glass_card_mc(cr, c.x, c.y, c.w, c.h, kCardR, s, mc);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  card_title(cr, mc, c, "insights", "System");

  dashboard_sample_system(app);

  char b[64];
  struct Row {
    const char* glyph;
    const char* label;
    std::string value;
    double frac;
  };
  Row rows[3];
  std::snprintf(b, sizeof(b), "%.1f%%", d.sysCpuPct);
  rows[0] = Row{"speed", "CPU", b, std::clamp(d.sysCpuPct / 100.0, 0.0, 1.0)};

  if (d.sysMemTotalMb > 0.0)
    std::snprintf(b, sizeof(b), "%.1f / %.1f GB", d.sysMemMb / 1024.0, d.sysMemTotalMb / 1024.0);
  else
    std::snprintf(b, sizeof(b), "--");
  rows[1] = Row{"memory", "Memory", b,
                d.sysMemTotalMb > 0.0 ? std::clamp(d.sysMemMb / d.sysMemTotalMb, 0.0, 1.0) : 0.0};

  const bool haveTemp = d.sysTempC > 0.5;
  if (haveTemp) std::snprintf(b, sizeof(b), "%.0f\u00b0C", d.sysTempC);
  else std::snprintf(b, sizeof(b), "--");
  rows[2] = Row{"thermostat", "Temperature", b, haveTemp ? std::clamp(d.sysTempC / 100.0, 0.0, 1.0) : 0.0};

  for (int i = 0; i < 3; ++i) {
    const double rowY = c.y + kSysHeaderH + static_cast<double>(i) * kSysRowH;
    eh::shell::draw_material_glyph(cr, c.x + 24.0, rowY + 20.0, 16.0, rows[i].glyph, 0.80, 0.86, 0.90, 1.0);
    text_l(cr, rows[i].label, c.x + 40.0, rowY + 24.0, 12.0, mc.textR, mc.textG, mc.textB, 0.95);
    text_r(cr, rows[i].value, c.x + c.w - 16.0, rowY + 24.0, 12.0, mc.textR, mc.textG, mc.textB, 1.0);

    const double barX = c.x + 40.0;
    const double barW = std::max(30.0, c.w - 56.0 - 40.0);
    const double barY = rowY + 32.0;
    rrect(cr, barX, barY, barW, 6.0, 3.0);
    cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.85 * s + 0.15);
    cairo_fill(cr);
    if (rows[i].frac > 0.005) {
      rrect(cr, barX, barY, std::max(4.0, barW * rows[i].frac), 6.0, 3.0);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.90);
      cairo_fill(cr);
    }
  }
}

// ------------------------------------------- expanded network/bluetooth ----

void paint_wifi_rows(DockApp& app, cairo_t* cr, const CardRect& c, const eh::config::ChromePaintColors& mc,
                     double innerGlass) {
  auto& d = app.dash;
  if (!d.netExpanded) return;
  const auto aps = dashboard_sorted_wifi_aps();
  const int rows = std::max(1, std::min(6, static_cast<int>(aps.size())));

  cairo_save(cr);
  cairo_rectangle(cr, c.x, c.y + kNetHeaderH, c.w, c.h - kNetHeaderH);
  cairo_clip(cr);

  text_l(cr, "Access points", c.x + 12.0, c.y + kNetHeaderH + 20.0, 12.0, mc.textR, mc.textG, mc.textB, 0.95);
  char cnt[16];
  std::snprintf(cnt, sizeof(cnt), "%d", static_cast<int>(aps.size()));
  text_r(cr, cnt, c.x + c.w - 12.0, c.y + kNetHeaderH + 20.0, 12.0, mc.textR, mc.textG, mc.textB, 0.75);

  for (int i = 0; i < rows && i < static_cast<int>(aps.size()); ++i) {
    const auto& ap = aps[static_cast<size_t>(i)];
    const double ry = cc::cc_panel_row_y(c.y + kNetHeaderH, i);
    const double bx = c.x + 10.0, bw = c.w - 20.0;
    const bool hot = d.hover.valid && d.hover.card >= 0 && d.hover.role == DashCardRole::Row &&
                     d.hover.row == i && d.hover.kind == DashCardKind::Network;
    if (hot) hover_hl(cr, mc, bx, ry + 4.0, bw, 26.0, 14.0);
    else if (ap.active) selected_bg(cr, mc, bx, ry + 4.0, bw, 26.0, 14.0);
    else row_bg(cr, bx, ry + 4.0, bw, 26.0, 14.0);

    eh::shell::draw_material_glyph(cr, bx + 12.0, ry + 17.0, 16.0, ap.active ? "wifi" : "wifi", 0.86, 0.90, 0.94, 1.0);
    // Measured budget: the old fixed `bw - 84` guess let a long SSID run into
    // the signal % (or leave a 3-char overflowing stub) on narrow cards.
    const std::string sig =
        ap.signal_pct >= 0 ? (std::to_string(ap.signal_pct) + "%") : std::string("?");
    cairo_set_font_size(cr, 11.0);
    const double sigW = ccu::cc_text_width(cr, sig);
    const double sigRight = bx + bw - 34.0;
    const double ssidX = bx + 30.0;
    const bool showSig = sigW + 6.0 <= sigRight - ssidX - 12.0;
    const double ssidBudget = showSig ? (sigRight - sigW - 6.0 - ssidX) : (sigRight - ssidX);
    text_l(cr, trunc(cr, ap.ssid.empty() ? std::string("(hidden)") : ap.ssid, std::max(0.0, ssidBudget), 12.0),
           ssidX, ry + 21.0, 12.0, mc.textR, mc.textG, mc.textB, 1.0);
    if (showSig)
      text_r(cr, sig, sigRight, ry + 21.0, 11.0, mc.textR, mc.textG, mc.textB, 0.90);
    if (ap.needs_password)
      eh::shell::draw_material_glyph(cr, bx + bw - 14.0, ry + 17.0, 15.0, "lock", 0.86, 0.90, 0.94, 1.0);
  }

  const uint64_t now = eh::shell::now_mono_ms();
  if (!d.wifiError.empty() && now < d.wifiErrorUntilMs)
    text_l(cr, trunc(cr, d.wifiError, c.w - 24.0, 11.0), c.x + 12.0, c.y + c.h - 6.0, 11.0, 0.95, 0.45, 0.45, 0.95);

  cairo_restore(cr);
  (void)innerGlass;
}

void paint_bt_rows(DockApp& app, cairo_t* cr, const CardRect& c, const eh::config::ChromePaintColors& mc) {
  auto& d = app.dash;
  if (!d.btExpanded) return;
  const auto devs = dashboard_sorted_bt_devices();
  const int rows = std::max(1, std::min(6, static_cast<int>(devs.size())));

  cairo_save(cr);
  cairo_rectangle(cr, c.x, c.y + kNetHeaderH, c.w, c.h - kNetHeaderH);
  cairo_clip(cr);

  text_l(cr, "Devices", c.x + 12.0, c.y + kNetHeaderH + 20.0, 12.0, mc.textR, mc.textG, mc.textB, 0.95);
  char cnt[16];
  std::snprintf(cnt, sizeof(cnt), "%d", static_cast<int>(devs.size()));
  text_r(cr, cnt, c.x + c.w - 12.0, c.y + kNetHeaderH + 20.0, 12.0, mc.textR, mc.textG, mc.textB, 0.75);

  const double rightEdge = c.x + c.w - 10.0;
  for (int i = 0; i < rows && i < static_cast<int>(devs.size()); ++i) {
    const auto& dv = devs[static_cast<size_t>(i)];
    const double ry = cc::cc_panel_row_y(c.y + kNetHeaderH, i);
    const double bx = c.x + 10.0, bw = c.w - 20.0;
    const bool hot = d.hover.valid && d.hover.card >= 0 && d.hover.role == DashCardRole::Row &&
                     d.hover.row == i && d.hover.kind == DashCardKind::Bluetooth;
    if (hot) hover_hl(cr, mc, bx, ry + 4.0, bw, 26.0, 14.0);
    else if (dv.connected) selected_bg(cr, mc, bx, ry + 4.0, bw, 26.0, 14.0);
    else row_bg(cr, bx, ry + 4.0, bw, 26.0, 14.0);

    const auto b = cc::cc_bt_btns(dv.connected, dv.paired, rightEdge);
    const char* actionText = dv.connected ? "Disconnect" : (dv.paired ? "Connect" : "Pair");
    const bool primary = dv.connected || dv.paired;
    const double btnH = 24.0;
    const double btnY = ry + (34.0 - btnH) * 0.5;
    rrect(cr, b.actionX, btnY, b.actionW, btnH, 8.0);
    if (primary) {
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.85);
      cairo_fill(cr);
      text_c(cr, actionText, b.actionX + b.actionW * 0.5, btnY + 15.5, 10.0, 0.05, 0.05, 0.07, 1.0);
    } else {
      cairo_set_source_rgba(cr, 0.22, 0.22, 0.28, 0.70);
      cairo_fill(cr);
      text_c(cr, actionText, b.actionX + b.actionW * 0.5, btnY + 15.5, 10.0, 0.86, 0.90, 0.94, 0.90);
    }

    const char* glyph = hooks::bluetooth_device_kind_glyph(dv.kind);
    eh::shell::draw_material_glyph(cr, bx + 12.0, ry + 17.0, 15.0, glyph,
                                   dv.connected ? 0.30 : 0.80, dv.connected ? 0.85 : 0.84,
                                   dv.connected ? 0.30 : 0.86, 1.0);
    const double labelX = bx + 32.0;
    const double nameMax = std::max(10.0, b.actionX - 6.0 - labelX);
    // Ellipsis truncation instead of the old hard cairo_clip, which sliced
    // names mid-glyph against the action button on narrow cards.
    text_l(cr, trunc(cr, dv.alias.empty() ? dv.address : dv.alias, nameMax, 12.0), labelX, ry + 21.0, 12.0,
           mc.textR, mc.textG, mc.textB, 1.0);
  }
  cairo_restore(cr);
}

// -------------------------------------------- compact net/bt headers (40px) ----
// Single-row headers for the half-height network/bluetooth cards. The shared
// control-center card painters draw a 76px two-line header, so the dashboard
// paints these instead (expanded rows below are unchanged).

void paint_net_compact(cairo_t* cr, const CardRect& c, const eh::config::ChromePaintColors& mc, double s) {
  const auto ns = hooks::control_center_network_state();
  ccu::cc_paint_glass_card_mc(cr, c.x, c.y, c.w, c.h, kCardR, s, mc);

  const double icy = c.y + 20.0;
  cairo_save(cr);
  cairo_arc(cr, c.x + 22.0, icy, 11.0, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, ns.connected ? 0.18 * s : 0.08 * s);
  cairo_fill(cr);
  cairo_restore(cr);

  const char* glyph = ns.connected ? (ns.wifi ? "wifi" : "lan") : "signal_wifi_off";
  const double gr = ns.connected ? mc.accentR : 0.65, gg = ns.connected ? mc.accentG : 0.68,
               gb = ns.connected ? mc.accentB : 0.72;
  eh::shell::draw_material_glyph(cr, c.x + 22.0, icy + 1.0, 16.0, glyph, gr, gg, gb, 0.96);

  const std::string title =
      ns.wifi ? (ns.ssid.empty() ? "Wi-Fi" : ns.ssid) : (ns.ethernet ? "Ethernet" : "Network");
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13.0);
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.95);
  cairo_move_to(cr, c.x + 42.0, c.y + 25.0);
  cairo_show_text(cr, trunc(cr, title, std::max(20.0, c.w - 160.0), 13.0).c_str());

  const std::string pill = ns.connected ? "Connected" : "Disconnected";
  ccu::cc_draw_status_pill(cr, c.x + c.w - 104.0, c.y + 9.0, pill, ns.connected, s, mc);
}

void paint_bt_compact(cairo_t* cr, const CardRect& c, const eh::config::ChromePaintColors& mc, double s) {
  const auto bs = hooks::control_center_bluetooth_state();
  ccu::cc_paint_glass_card_mc(cr, c.x, c.y, c.w, c.h, kCardR, s, mc);

  const double icy = c.y + 20.0;
  cairo_save(cr);
  cairo_arc(cr, c.x + 22.0, icy, 11.0, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, bs.powered ? 0.18 * s : 0.08 * s);
  cairo_fill(cr);
  cairo_restore(cr);

  const char* glyph = bs.powered ? "bluetooth" : "bluetooth_disabled";
  const double gr = bs.powered ? mc.accentR : 0.65, gg = bs.powered ? mc.accentG : 0.68,
               gb = bs.powered ? mc.accentB : 0.72;
  eh::shell::draw_material_glyph(cr, c.x + 22.0, icy + 1.0, 16.0, glyph, gr, gg, gb, 0.96);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13.0);
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.95);
  cairo_move_to(cr, c.x + 42.0, c.y + 25.0);
  cairo_show_text(cr, trunc(cr, "Bluetooth", std::max(20.0, c.w - 160.0), 13.0).c_str());

  ccu::cc_draw_status_pill(cr, c.x + c.w - 112.0, c.y + 9.0, bs.powered ? "On" : "Off", bs.powered, s, mc);
}

// ----------------------------------------------------------------- card ----

void paint_card(DockApp& app, cairo_t* cr, const CardRect& c, const eh::config::ChromePaintColors& mc, double inner) {
  auto& d = app.dash;
  switch (c.kind) {
    case DashCardKind::Clock:
      paint_clock(app, cr, c, inner);
      break;
    case DashCardKind::Calendar:
      paint_calendar(app, cr, c, inner);
      break;
    case DashCardKind::Weather:
      paint_weather(app, cr, c, inner);
      break;
    case DashCardKind::Media:
      paint_media(app, cr, c, inner);
      break;
    case DashCardKind::Mixer:
      paint_mixer(app, cr, c, inner);
      break;
    case DashCardKind::System:
      paint_system(app, cr, c, inner);
      break;
    case DashCardKind::Volume: {
      auto as = hooks::control_center_audio_output_state();
      if (d.dragging && d.dragKind == DashDragKind::Volume && d.dragVisualT >= 0.0)
        as.volume_fill_t_override = std::clamp(d.dragVisualT, 0.0, 1.0);
      hooks::paint_control_center_audio_output_card(cr, c.x, c.y, c.w, c.h, as, inner, kCardR);
      break;
    }
    case DashCardKind::Mic: {
      auto as = hooks::control_center_audio_input_state();
      if (d.dragging && d.dragKind == DashDragKind::InputVolume && d.dragVisualT >= 0.0)
        as.volume_fill_t_override = std::clamp(d.dragVisualT, 0.0, 1.0);
      hooks::paint_control_center_audio_input_card(cr, c.x, c.y, c.w, c.h, as, inner, kCardR);
      break;
    }
    case DashCardKind::Network: {
      paint_net_compact(cr, c, mc, inner);
      paint_wifi_rows(app, cr, c, mc, inner);
      break;
    }
    case DashCardKind::Bluetooth: {
      paint_bt_compact(cr, c, mc, inner);
      paint_bt_rows(app, cr, c, mc);
      break;
    }
    default:
      break;
  }
}

}  // namespace

void dashboard_paint(DockApp& app, cairo_t* cr, double surfaceW, double surfaceH) {
  auto& d = app.dash;
  if (!cr) return;

  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  d.layout = dashboard_compute_layout(app, surfaceW);
  const DashboardLayout& L = d.layout;
  if (!L.valid || !d.cfg.enabled) return;
  if (d.revealT <= 0.f && !d.open) return;  // fully hidden: transparent buffer only

  if (d.calYear == 0 || d.calMonth == 0) {
    const auto today = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(
        std::chrono::system_clock::now())};
    d.calYear = static_cast<int>(today.year());
    d.calMonth = static_cast<int>(static_cast<unsigned>(today.month()));
  }

  const auto& sc = eh::config::shell_config_snapshot();
  const double shellOv = static_cast<double>(eh::config::overlay_surface_alpha_scale(
      sc, eh::config::OverlaySurfaceAlphaKind::ControlCenter));
  const double innerGlass = static_cast<double>(eh::config::control_center_inner_glass_alpha_scale(sc));
  const double s = shellOv * innerGlass;
  const auto mc = eh::config::derived_chrome_colors(sc.appearance);

  cairo_save(cr);
  cairo_translate(cr, 0.0, -(L.panelY + L.panelH) * (1.0 - static_cast<double>(d.revealT)));

  // Panel backdrop — the dashboard's own glass slab behind the card grid.
  ccu::cc_paint_glass_card_mc(cr, L.panelX, L.panelY, L.panelW, L.panelH, kPanelR, 0.80 * s, mc);

  for (const auto& c : L.cards) {
    cairo_save(cr);
    cairo_rectangle(cr, c.x, c.y, c.w, c.h);
    cairo_clip(cr);
    paint_card(app, cr, c, mc, s);
    cairo_restore(cr);
  }

  // Hover feedback on top of every card.
  if (d.hover.valid && d.hover.card >= 0 && d.hover.card < static_cast<int>(L.cards.size())) {
    const CardRect& c = L.cards[static_cast<size_t>(d.hover.card)];
    if (d.hover.sub >= 0 && d.hover.sub < static_cast<int>(c.subs.size())) {
      const SubRect& sr = c.subs[static_cast<size_t>(d.hover.sub)];
      const double r = (sr.role == DashCardRole::Mute || sr.role == DashCardRole::MediaPrev ||
                        sr.role == DashCardRole::MediaPlayPause || sr.role == DashCardRole::MediaNext ||
                        sr.role == DashCardRole::CalPrev || sr.role == DashCardRole::CalNext ||
                        sr.role == DashCardRole::CalToday)
                           ? 14.0
                           : 16.0;
      hover_hl(cr, mc, sr.paintX, sr.paintY, sr.paintW, sr.paintH, r);
    } else {
      hover_hl(cr, mc, c.x, c.y, c.w, c.h, kCardR);
    }
  }

  cairo_restore(cr);
  (void)surfaceH;
}

}  // namespace eh::shell::dashboard
