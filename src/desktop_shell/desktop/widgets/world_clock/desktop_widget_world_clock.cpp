#include "desktop_shell/desktop/widgets/world_clock/desktop_widget_world_clock.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/paint/glass_card_style.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace eh::shell::desktop {
namespace {

static std::string world_clock_config_path() {
  if (const char* h = std::getenv("HOME")) {
    return std::string(h) + "/.local/state/event-horizon/WorldClock";
  }
  return "/tmp/event-horizon/WorldClock";
}

struct CityEntry {
  std::string label;
  std::string timezone;
  bool darkTheme;
};

static void write_default_config(const std::string& path) {
  try {
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    std::ofstream ofs(path);
    if (!ofs) return;
    ofs << "city_1_tz = \"America/Los_Angeles\"\n";
    ofs << "city_1_label = \"Cupertino\"\n";
    ofs << "city_2_tz = \"Asia/Tokyo\"\n";
    ofs << "city_2_label = \"Tokyo\"\n";
    ofs << "city_3_tz = \"Australia/Sydney\"\n";
    ofs << "city_3_label = \"Sydney\"\n";
    ofs << "city_4_tz = \"Europe/Paris\"\n";
    ofs << "city_4_label = \"Paris\"\n";
  } catch (...) {
  }
}

static std::vector<CityEntry> load_cities() {
  std::vector<CityEntry> cities;
  const std::string path = world_clock_config_path();
  if (!std::filesystem::exists(path)) {
    write_default_config(path);
  }
  try {
    toml::table tbl = toml::parse_file(path);
    for (int idx = 1;; idx++) {
      const std::string tzKey = "city_" + std::to_string(idx) + "_tz";
      const auto* tzVal = tbl.get_as<std::string>(tzKey);
      if (!tzVal) break;
      const std::string tz = tzVal->get();
      const std::string labelKey = "city_" + std::to_string(idx) + "_label";
      const auto* labelVal = tbl.get_as<std::string>(labelKey);
      std::string label;
      if (labelVal) {
        label = labelVal->get();
      } else {
        auto pos = tz.find('/');
        label = (pos != std::string::npos) ? tz.substr(pos + 1) : tz;
        if (label.size() > 8) label = label.substr(0, 8);
      }
      cities.push_back({label, tz, idx > 1});
    }
  } catch (const toml::parse_error&) {
  }
  if (cities.empty()) {
    cities.push_back({"Cupertino", "America/Los_Angeles", false});
    cities.push_back({"Tokyo", "Asia/Tokyo", true});
    cities.push_back({"Sydney", "Australia/Sydney", true});
    cities.push_back({"Paris", "Europe/Paris", true});
  }
  return cities;
}

struct CityTimeData {
  int hour24;
  int minute;
  int second;
  int yday;
  int year;
  long utcOffsetSec;
};

static CityTimeData get_city_time(const std::string& tz) {
  std::time_t now = std::time(nullptr);
  CityTimeData d{};
  d.utcOffsetSec = 0;

  if (tz.empty()) {
    std::tm local{};
    localtime_r(&now, &local);
    d.hour24 = local.tm_hour;
    d.minute = local.tm_min;
    d.second = local.tm_sec;
    d.yday = local.tm_yday;
    d.year = local.tm_year;
    return d;
  }

  // Thread-safe timezone lookup using std::chrono instead of setenv("TZ").
  const auto* tzDb = std::chrono::locate_zone(tz);
  if (tzDb == nullptr) {
    std::tm local{};
    localtime_r(&now, &local);
    d.hour24 = local.tm_hour;
    d.minute = local.tm_min;
    d.second = local.tm_sec;
    d.yday = local.tm_yday;
    d.year = local.tm_year;
    return d;
  }

  const auto zt = std::chrono::zoned_time(tzDb, std::chrono::system_clock::from_time_t(now));
  const auto lt = zt.get_local_time();
  const auto tp = std::chrono::floor<std::chrono::seconds>(lt);
  const auto dp = std::chrono::floor<std::chrono::days>(tp);
  std::chrono::hh_mm_ss hms{tp - dp};
  std::chrono::year_month_day ymd{dp};

  d.hour24 = static_cast<int>(hms.hours().count());
  d.minute = static_cast<int>(hms.minutes().count());
  d.second = static_cast<int>(hms.seconds().count());
  d.year = static_cast<int>(ymd.year()) - 1900;
  const auto ymdTp = std::chrono::sys_days(ymd);
  const auto jan1Tp = std::chrono::sys_days(std::chrono::year_month_day{
      ymd.year(), std::chrono::month{1}, std::chrono::day{1}});
  d.yday = static_cast<int>((ymdTp - jan1Tp).count());

  const auto offset = zt.get_info().offset;
  d.utcOffsetSec = static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(offset).count());

  return d;
}

static void draw_clock_face(cairo_t* cr, double cx, double cy, double r,
                             int hour24, int minute, int second, bool dark) {
  const double bgR = dark ? 0.17 : 0.96;
  const double bgG = dark ? 0.17 : 0.96;
  const double bgB = dark ? 0.17 : 0.96;
  const double fgR = dark ? 0.90 : 0.15;
  const double fgG = dark ? 0.90 : 0.15;
  const double fgB = dark ? 0.90 : 0.15;

  cairo_save(cr);

  cairo_set_source_rgba(cr, bgR, bgG, bgB, 1.0);
  cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
  cairo_fill(cr);

  if (dark) {
    cairo_set_source_rgba(cr, 0.23, 0.23, 0.24, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
    cairo_stroke(cr);
  }

  for (int i = 0; i < 60; i++) {
    const double angle = (static_cast<double>(i) * 6.0 - 90.0) * M_PI / 180.0;
    const bool isHour = (i % 5 == 0);
    const double innerR = isHour ? r - r * 0.20 : r - r * 0.10;
    const double outerR = r - 1.5;
    const double x1 = cx + outerR * std::cos(angle);
    const double y1 = cy + outerR * std::sin(angle);
    const double x2 = cx + innerR * std::cos(angle);
    const double y2 = cy + innerR * std::sin(angle);
    if (!isHour) {
      cairo_set_source_rgba(cr, fgR, fgG, fgB, 0.35);
      cairo_set_line_width(cr, 0.6);
    } else {
      cairo_set_source_rgba(cr, fgR, fgG, fgB, 0.8);
      cairo_set_line_width(cr, 1.2);
    }
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
  }

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, r * 0.20);
  for (int n = 1; n <= 12; n++) {
    const double angle = (static_cast<double>(n) * 30.0 - 90.0) * M_PI / 180.0;
    const double nr = r - r * 0.28;
    const double nx = cx + nr * std::cos(angle);
    const double ny = cy + nr * std::sin(angle);
    cairo_set_source_rgba(cr, fgR, fgG, fgB, 0.85);
    cairo_text_extents_t te;
    char digit[4];
    std::snprintf(digit, sizeof(digit), "%d", n);
    cairo_text_extents(cr, digit, &te);
    cairo_move_to(cr, nx - te.width / 2.0 - te.x_bearing, ny + te.height / 2.0);
    cairo_show_text(cr, digit);
  }

  int h12 = hour24 % 12;
  if (h12 == 0) h12 = 12;
  const double hourAngle = (static_cast<double>(h12) * 30.0 + static_cast<double>(minute) * 0.5 - 90.0) * M_PI / 180.0;
  const double minAngle = (static_cast<double>(minute) * 6.0 + static_cast<double>(second) * 0.1 - 90.0) * M_PI / 180.0;
  const double secAngle = (static_cast<double>(second) * 6.0 - 90.0) * M_PI / 180.0;

  const double hourLen = r * 0.48;
  const double minLen = r * 0.68;
  const double secLen = r * 0.76;

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_set_source_rgba(cr, fgR, fgG, fgB, 0.9);
  cairo_set_line_width(cr, r * 0.065);
  cairo_move_to(cr, cx, cy);
  cairo_line_to(cr, cx + hourLen * std::cos(hourAngle), cy + hourLen * std::sin(hourAngle));
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, fgR, fgG, fgB, 0.85);
  cairo_set_line_width(cr, r * 0.040);
  cairo_move_to(cr, cx, cy);
  cairo_line_to(cr, cx + minLen * std::cos(minAngle), cy + minLen * std::sin(minAngle));
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, 1.0, 0.62, 0.04, 0.9);
  cairo_set_line_width(cr, r * 0.020);
  cairo_move_to(cr, cx - r * 0.12 * std::cos(secAngle), cy - r * 0.12 * std::sin(secAngle));
  cairo_line_to(cr, cx + secLen * std::cos(secAngle), cy + secLen * std::sin(secAngle));
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, fgR, fgG, fgB, 0.95);
  cairo_arc(cr, cx, cy, r * 0.050, 0, 2.0 * M_PI);
  cairo_fill(cr);

  cairo_restore(cr);
}

static void draw_centered_text(cairo_t* cr, const std::string& text, double cx, double y,
                                double r, double g, double b, double alpha, double fontSize, int weight) {
  if (text.empty()) return;
  cairo_save(cr);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                          weight >= 700 ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, fontSize);
  cairo_text_extents_t te;
  cairo_text_extents(cr, text.c_str(), &te);
  cairo_set_source_rgba(cr, r, g, b, alpha);
  cairo_move_to(cr, cx - te.width / 2.0 - te.x_bearing, y);
  cairo_show_text(cr, text.c_str());
  cairo_restore(cr);
}

static std::string format_offset_label(long cityOffsetSec, long localOffsetSec) {
  const double diffHours = (static_cast<double>(cityOffsetSec) - static_cast<double>(localOffsetSec)) / 3600.0;
  if (std::abs(diffHours) < 0.01) return "Same";
  const double rounded = std::round(diffHours * 2.0) / 2.0;
  char buf[16];
  if (rounded > 0) std::snprintf(buf, sizeof(buf), "+%.1fH", rounded);
  else std::snprintf(buf, sizeof(buf), "%.1fH", rounded);
  return buf;
}

static std::string format_day_label(int cityYday, int cityYear, int localYday, int localYear) {
  if (cityYear == localYear) {
    const int diff = cityYday - localYday;
    if (diff == 0) return "Today";
    if (diff == 1) return "Tomorrow";
    if (diff == -1) return "Yesterday";
  }
  return "";
}

}

DesktopWorldClockWidget::DesktopWorldClockWidget(std::string widgetId)
    : m_widgetId(std::move(widgetId)) {}

void DesktopWorldClockWidget::create() {
}

void DesktopWorldClockWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);

  const auto cities = load_cities();
  if (cities.empty()) return;

  CityTimeData localData = get_city_time("");

  struct CityRenderData {
    CityEntry entry;
    CityTimeData timeData;
  };
  std::vector<CityRenderData> renderData;
  for (const auto& c : cities) {
    renderData.push_back({c, get_city_time(c.timezone)});
  }

  const int n = static_cast<int>(renderData.size());
  const double clockR = 42.0 * us;
  const double cellW = clockR * 2.0 + 16.0 * us;
  const double cellGap = 18.0 * us;
  const double padX = 18.0 * us;
  const double padY = 16.0 * us;

  const double cityFont = 14.0 * us;
  const double dimFont = 12.0 * us;
  const double cityGap = 8.0 * us;
  const double lineGap = 2.0 * us;

  const double labelAreaH = cityGap + cityFont + lineGap + dimFont + lineGap + dimFont + 4.0 * us;
  const double totalW = static_cast<double>(n) * cellW + static_cast<double>(n - 1) * cellGap + padX * 2.0;
  const double totalH = padY + clockR * 2.0 + 4.0 * us + labelAreaH + padY;

  m_width = static_cast<int>(std::ceil(totalW));
  m_height = static_cast<int>(std::ceil(totalH));

  cairo_save(cr);

  eh::shell::shared::paint_glass_card(cr, 0, 0, totalW, totalH, 14.0 * us, mc,
                                      sc.appearance.overlayOpacityWidgetCard);

  for (int i = 0; i < n; i++) {
    const auto& rd = renderData[i];
    const double cellX = padX + static_cast<double>(i) * (cellW + cellGap);
    const double faceCx = cellX + cellW * 0.5;
    const double faceCy = padY + clockR;
    const double faceR = clockR;

    draw_clock_face(cr, faceCx, faceCy, faceR,
                    rd.timeData.hour24, rd.timeData.minute, rd.timeData.second,
                    rd.entry.darkTheme);

    const double textY0 = faceCy + faceR + 4.0 * us + cityGap;
    draw_centered_text(cr, rd.entry.label, faceCx, textY0,
                       1.0, 1.0, 1.0, 0.95, cityFont, 700);

    const double dayY = textY0 + cityFont + lineGap;
    const std::string dayLabel = format_day_label(rd.timeData.yday, rd.timeData.year,
                                                   localData.yday, localData.year);
    draw_centered_text(cr, dayLabel, faceCx, dayY,
                       0.60, 0.60, 0.62, 0.8, dimFont, 500);

    const double offY = dayY + dimFont + lineGap;
    const std::string offLabel = format_offset_label(rd.timeData.utcOffsetSec, localData.utcOffsetSec);
    draw_centered_text(cr, offLabel, faceCx, offY,
                       0.60, 0.60, 0.62, 0.8, dimFont, 500);
  }

  cairo_restore(cr);
}

bool DesktopWorldClockWidget::on_click(double /*x*/, double /*y*/) {
  return false;
}

void DesktopWorldClockWidget::on_motion(double /*x*/, double /*y*/) {
}

}
