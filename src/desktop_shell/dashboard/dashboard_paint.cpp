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
#include <cctype>
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

constexpr double kCardR = 14.0;
constexpr double kPanelR = 16.0;
constexpr double kMutR = 0.42, kMutG = 0.44, kMutB = 0.46;  // eyebrow labels
constexpr double kNetHeaderH = 40.0;
constexpr double kMixerHeaderH = sm::kMixerHeaderH;
constexpr double kMixerRowH = sm::kMixerRowH;

const char* kDowShort[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

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

std::string upper_str(const std::string& s) {
  std::string out = s;
  for (char& ch : out) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return out;
}

void eyebrow(cairo_t* cr, const std::string& s, double x, double baseline) {
  text_l(cr, s, x, baseline, 11.0, kMutR, kMutG, kMutB, 1.0);
}

// ------------------------------------------------------- calendar (week) ----
// Current-week strip (v5): seven day cells for the week containing today,
// today highlighted. No navigation state.

void paint_clock(DockApp& app, cairo_t* cr, const CardRect& c, double s) {
  (void)app;
  const auto mc = eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  ccu::cc_paint_glass_card_mc(cr, c.x, c.y, c.w, c.h, kCardR, s, mc);

  const DashClockText t = dashboard_clock_text(eh::config::shell_config_snapshot());

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  eyebrow(cr, "LOCAL TIME", c.x + 16.0, c.y + 27.0);
  text_l(cr, t.time, c.x + 16.0, c.y + 66.0, 32.0, mc.textR, mc.textG, mc.textB, 1.0);
  if (!t.date.empty())
    text_l(cr, t.date, c.x + 16.0, c.y + 88.0, 13.0, mc.textR, mc.textG, mc.textB, 0.90);
}

// ------------------------------------------------------------- calendar ----

void paint_calendar(DockApp& app, cairo_t* cr, const CardRect& c, double s) {
  (void)app;
  const auto mc = eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  ccu::cc_paint_glass_card_mc(cr, c.x, c.y, c.w, c.h, kCardR, s, mc);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  eyebrow(cr, "THIS WEEK", c.x + 16.0, c.y + 27.0);

  const std::chrono::sys_days today =
      std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
  const unsigned dow = static_cast<unsigned>(std::chrono::weekday{today}.c_encoding());
  const std::chrono::sys_days weekStart = today - std::chrono::days{dow};

  constexpr double kPadX = 16.0;
  constexpr double kGap = 6.0;
  constexpr double kCellH = 48.0;
  const double gridTop = c.y + 43.0;
  const double colW = std::max(24.0, (c.w - 2.0 * kPadX - 6.0 * kGap) / 7.0);
  for (int i = 0; i < 7; ++i) {
    const std::chrono::sys_days dd = weekStart + std::chrono::days{i};
    const int num = static_cast<int>(static_cast<unsigned>(std::chrono::year_month_day{dd}.day()));
    const double cx0 = c.x + kPadX + static_cast<double>(i) * (colW + kGap);
    if (dd == today) {
      rrect(cr, cx0, gridTop, colW, kCellH, 8.0);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.90);
      cairo_fill(cr);
      text_c(cr, kDowShort[i], cx0 + colW * 0.5, gridTop + 18.0, 10.0, 0.05, 0.05, 0.07, 1.0);
      text_c(cr, std::to_string(num), cx0 + colW * 0.5, gridTop + 36.0, 14.0, 0.05, 0.05, 0.07, 1.0);
    } else {
      text_c(cr, kDowShort[i], cx0 + colW * 0.5, gridTop + 18.0, 10.0, kMutR, kMutG, kMutB, 1.0);
      text_c(cr, std::to_string(num), cx0 + colW * 0.5, gridTop + 36.0, 14.0, mc.textR, mc.textG, mc.textB, 1.0);
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

  std::string city = ws.location;
  const auto p = city.find(',');
  if (p != std::string::npos) city = city.substr(0, p);
  if (city.empty()) city = "No location";
  eyebrow(cr, upper_str(trunc(cr, city, std::max(20.0, c.w - 32.0), 11.0)), c.x + 16.0, c.y + 27.0);

  const double cy = c.y + 52.0;
  eh::shell::draw_material_glyph(cr, c.x + 30.0, cy, 22.0, glyph, ws.available ? mc.accentR : 0.65,
                                 ws.available ? mc.accentG : 0.68, ws.available ? mc.accentB : 0.72, 1.0);
  text_l(cr, ws.available ? (std::to_string(ws.temp) + "\u00b0" + unitCh) : "--", c.x + 48.0, cy + 8.0, 22.0,
         mc.textR, mc.textG, mc.textB, 1.0);

  const std::string sub = std::string("Feels ") + (ws.available ? (std::to_string(ws.feels_like) + unitCh) : "--") +
                          " \u00b7 Wind " + (ws.available ? (std::to_string(ws.wind_kmh) + " km/h") : "--");
  text_l(cr, trunc(cr, sub, std::max(20.0, c.w - 32.0), 12.0), c.x + 16.0, c.y + 80.0, 12.0, mc.textR, mc.textG,
         mc.textB, 0.90);
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

  const double cy = dashboard_media_row_cy(c);
  const double artS = 48.0;
  const double artX = c.x + 16.0, artY = cy - artS * 0.5;
  rrect(cr, artX, artY, artS, artS, 8.0);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.07);
  cairo_fill(cr);
  if (active && ms.art) {
    cairo_save(cr);
    rrect(cr, artX, artY, artS, artS, 8.0);
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
    eh::shell::draw_material_glyph(cr, artX + artS * 0.5, artY + artS * 0.55, 22.0, "music_note", 0.88, 0.93, 0.96,
                                   1.0);
  }

  const double textX = c.x + 78.0;
  const double textMaxW = std::max(20.0, (c.x + c.w - 124.0) - textX);
  text_l(cr, trunc(cr, active ? (ms.title.empty() ? std::string("Unknown title") : ms.title) : std::string("No Media"),
                   textMaxW, 14.0),
         textX, cy - 12.0, 14.0, mc.textR, mc.textG, mc.textB, 1.0);
  text_l(cr, trunc(cr, active ? (ms.artist.empty() ? std::string("Unknown artist") : ms.artist) : std::string("—"),
                   textMaxW, 12.0),
         textX, cy + 4.0, 12.0, mc.textR, mc.textG, mc.textB, 0.92);

  if (active && ms.duration_us > 0) {
    const DashMediaProgressGeom g = dashboard_media_progress_geom(c);
    double frac = std::clamp(static_cast<double>(ms.position_us) / static_cast<double>(ms.duration_us), 0.0, 1.0);
    if (d.dragging && d.dragKind == DashDragKind::Seek && d.dragVisualT >= 0.0)
      frac = std::clamp(d.dragVisualT, 0.0, 1.0);
    rrect(cr, g.trackX, g.trackY, g.trackW, g.trackH, 1.5);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
    cairo_fill(cr);
    const double fillW = g.trackW * frac;
    if (fillW > 0.5) {
      rrect(cr, g.trackX, g.trackY, std::max(3.0, fillW), g.trackH, 1.5);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.90);
      cairo_fill(cr);
    }
  }

  if (active) {
    auto btn = [&](int idx, const char* glyph, bool enabled, double size, double alpha) {
      const double cx = dashboard_media_btn_cx(c, idx);
      eh::shell::draw_material_glyph(cr, cx, cy + 0.5, size, glyph, mc.textR, mc.textG, mc.textB,
                                     enabled ? alpha : 0.35);
    };
    btn(0, "skip_previous", ms.can_go_previous, 16.0, 0.90);
    btn(1, playing ? "pause" : "play_arrow", ms.can_play || ms.can_pause, 22.0, 1.0);
    btn(2, "skip_next", ms.can_go_next, 16.0, 0.90);
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

  eyebrow(cr, "VOLUME MIXER", c.x + 16.0, c.y + 27.0);

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
  eyebrow(cr, "SYSTEM", c.x + 16.0, c.y + 27.0);

  dashboard_sample_system(app);

  char b[64];
  std::snprintf(b, sizeof(b), "%.0f%%", d.sysCpuPct);
  const std::string cpuV = b;
  const double cpuFrac = std::clamp(d.sysCpuPct / 100.0, 0.0, 1.0);
  if (d.sysMemTotalMb > 0.0)
    std::snprintf(b, sizeof(b), "%.1f/%.1f GB", d.sysMemMb / 1024.0, d.sysMemTotalMb / 1024.0);
  else
    std::snprintf(b, sizeof(b), "--");
  const std::string memV = b;
  const double memFrac =
      d.sysMemTotalMb > 0.0 ? std::clamp(d.sysMemMb / d.sysMemTotalMb, 0.0, 1.0) : 0.0;

  const double barX = c.x + 16.0;
  const double barW = std::max(30.0, c.w - 32.0);
  auto bar_row = [&](double labelBase, const char* label, const std::string& value, double frac) {
    text_l(cr, label, barX, labelBase, 12.0, mc.textR, mc.textG, mc.textB, 0.90);
    text_r(cr, value, barX + barW, labelBase, 12.0, mc.textR, mc.textG, mc.textB, 1.0);
    rrect(cr, barX, labelBase + 3.0, barW, 4.0, 2.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
    cairo_fill(cr);
    if (frac > 0.005) {
      rrect(cr, barX, labelBase + 3.0, std::max(4.0, barW * frac), 4.0, 2.0);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.90);
      cairo_fill(cr);
    }
  };
  bar_row(c.y + 47.0, "CPU", cpuV, cpuFrac);
  bar_row(c.y + 74.0, "Memory", memV, memFrac);
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

// -------------------------------------------- compact volume/mic rows (84px) ----
// Eyebrow device label on top, then mute icon + flat bar + percent. Replaces
// the shared two-line audio cards (same mute/slider behavior, new geometry).

void paint_audio_compact(DockApp& app, cairo_t* cr, const CardRect& c,
                         const eh::config::ChromePaintColors& mc, double s, bool isInput) {
  auto& d = app.dash;
  bool muted = false;
  int volPct = 0;
  std::string device;
  if (isInput) {
    const auto as = hooks::control_center_audio_input_state();
    muted = as.muted;
    volPct = as.volume_pct;
    device = as.device_name;
  } else {
    const auto as = hooks::control_center_audio_output_state();
    muted = as.muted;
    volPct = as.volume_pct;
    device = as.device_name;
  }
  const DashAudioGeom g = dashboard_audio_geom(c);

  double frac = std::clamp(static_cast<double>(volPct) / 100.0, 0.0, 1.0);
  int pct = volPct;
  const DashDragKind wantKind = isInput ? DashDragKind::InputVolume : DashDragKind::Volume;
  if (d.dragging && d.dragKind == wantKind && d.dragVisualT >= 0.0) {
    frac = std::clamp(d.dragVisualT, 0.0, 1.0);
    pct = static_cast<int>(std::lround(frac * 100.0));
  }

  ccu::cc_paint_glass_card_mc(cr, c.x, c.y, c.w, c.h, kCardR, s, mc);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  if (device.empty()) device = "Unknown";
  eyebrow(cr, trunc(cr, (isInput ? std::string("INPUT \u00b7 ") : std::string("OUTPUT \u00b7 ")) + upper_str(device),
                    std::max(20.0, c.w - 32.0), 11.0),
          c.x + 16.0, c.y + 27.0);

  const char* glyph;
  if (isInput) {
    glyph = muted ? "mic_off" : "mic";
  } else {
    glyph = (muted || pct <= 0) ? "volume_off" : (pct < 34 ? "volume_down" : "volume_up");
  }
  const double igr = muted ? 0.65 : mc.accentR;
  const double igg = muted ? 0.68 : mc.accentG;
  const double igb = muted ? 0.72 : mc.accentB;
  eh::shell::draw_material_glyph(cr, g.iconCX, g.iconCY + 1.0, 18.0, glyph, igr, igg, igb, 0.96);

  rrect(cr, g.trackX, g.trackY, g.trackW, g.trackH, 1.5);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
  cairo_fill(cr);
  if (frac > 0.005) {
    rrect(cr, g.trackX, g.trackY, std::max(3.0, g.trackW * frac), g.trackH, 1.5);
    cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.90);
    cairo_fill(cr);
  }

  char pb[32];
  std::snprintf(pb, sizeof(pb), "%d%%", pct);
  text_r(cr, pb, g.pctRightX, g.pctBase, 12.0, mc.textR, mc.textG, mc.textB, 0.90);
}

// ----------------------------------------------------------------- card ----

void paint_card(DockApp& app, cairo_t* cr, const CardRect& c, const eh::config::ChromePaintColors& mc, double inner) {
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
      paint_audio_compact(app, cr, c, mc, inner, false);
      break;
    }
    case DashCardKind::Mic: {
      paint_audio_compact(app, cr, c, mc, inner, true);
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
                        sr.role == DashCardRole::MediaPlayPause || sr.role == DashCardRole::MediaNext)
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
