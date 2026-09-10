#include "desktop_shell/desktop/widgets/clock/desktop_widget_clock.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/dock/core/dock_app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>

#include <pango/pangocairo.h>

namespace eh::shell::desktop {
namespace {

void append_weekday_short(std::ostringstream& os, const std::tm& tm) {
   
  static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  if (tm.tm_wday >= 0 && tm.tm_wday <= 6) os << kDays[tm.tm_wday];
}

void append_date_formatted(std::ostringstream& os, const std::tm& tm, int dateFormat) {
   
  switch (dateFormat) {
    case 1: {
      static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
      append_weekday_short(os, tm);
      os << ' ' << tm.tm_mday << ' ';
      if (tm.tm_mon >= 0 && tm.tm_mon <= 11) os << kMonths[tm.tm_mon];
      os << ' ' << (tm.tm_year + 1900);
      break;
    }
    case 2: {
      char buf[32];
      if (std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm) > 0) os << buf;
      break;
    }
    default: {
      append_weekday_short(os, tm);
      os << ' ' << tm.tm_mday;
      break;
    }
  }
}

}

DesktopClockWidget::DesktopClockWidget(std::string timeFormat, bool showSeconds, bool showDate, int fontSize)
    : m_format(std::move(timeFormat)), m_showsSeconds(showSeconds), m_showsDate(showDate), m_fontSize(fontSize) {}

void DesktopClockWidget::create() {
   
  m_lastText.clear();
}

static std::string build_clock_text(const eh::config::ShellConfig& sc, const std::string& perWidgetFormat) {
   
  constexpr const char* kStd24 = "%H:%M";
  constexpr const char* kStd24s = "%H:%M:%S";
  constexpr const char* kStd12 = "%I:%M %p";
  constexpr const char* kStd12s = "%I:%M:%S %p";

  std::string fmt;
  const bool isStdFmt = perWidgetFormat == kStd24 || perWidgetFormat == kStd24s ||
                         perWidgetFormat == kStd12 || perWidgetFormat == kStd12s;
  if (!perWidgetFormat.empty() && !isStdFmt)
    fmt = perWidgetFormat;
  else if (!sc.time.customFormat.empty())
    fmt = sc.time.customFormat;
  else
    fmt = sc.time.use24h ? (sc.time.showSeconds ? kStd24s : kStd24)
                         : (sc.time.showSeconds ? kStd12s : kStd12);

  std::time_t now = std::time(nullptr);
  std::tm local{};
  {
    const std::string& tz = sc.time.timezone;
    if (!tz.empty()) {
      // Thread-safe timezone lookup using std::chrono instead of setenv("TZ").
      const auto* tzDb = std::chrono::locate_zone(tz);
      if (tzDb != nullptr) {
        const auto zt = std::chrono::zoned_time(tzDb, std::chrono::system_clock::from_time_t(now));
        const auto lt = zt.get_local_time();
        const auto tp = std::chrono::floor<std::chrono::seconds>(lt);
        const auto dp = std::chrono::floor<std::chrono::days>(tp);
        std::chrono::hh_mm_ss hms{tp - dp};
        std::chrono::year_month_day ymd{dp};
        local.tm_year = static_cast<int>(ymd.year()) - 1900;
        local.tm_mon = static_cast<unsigned>(ymd.month()) - 1;
        local.tm_mday = static_cast<unsigned>(ymd.day());
        local.tm_wday = static_cast<int>(std::chrono::weekday{dp}.c_encoding());
        local.tm_hour = static_cast<int>(hms.hours().count());
        local.tm_min = static_cast<int>(hms.minutes().count());
        local.tm_sec = static_cast<int>(hms.seconds().count());
      } else {
        if (localtime_r(&now, &local) == nullptr) return "??:??";
      }
    } else {
      if (localtime_r(&now, &local) == nullptr) return "??:??";
    }
  }

  char buf[128];
  if (std::strftime(buf, sizeof(buf), fmt.c_str(), &local) == 0) {
    std::snprintf(buf, sizeof(buf), "??:??");
  }
  std::string text = buf;

  if (sc.time.showDate && !fmt.empty()) {
    std::ostringstream date;
    append_date_formatted(date, local, sc.time.dateFormat);
    text += "\n";
    text += date.str();
  }

  return text;
}

void DesktopClockWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const std::string text = build_clock_text(sc, m_format);
  m_lastText = text;

  const double fontPx = static_cast<double>(m_fontSize > 0 ? m_fontSize : 14.0 * us);

  cairo_save(cr);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, fontPx);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.92);

  cairo_text_extents_t te;
  cairo_text_extents(cr, text.c_str(), &te);

  const int newlines = static_cast<int>(std::count(text.begin(), text.end(), '\n'));
  const double lineH = fontPx * 1.2;
  m_width = static_cast<int>(std::ceil(te.width + 8.0 * us));
  m_height = static_cast<int>(std::ceil(lineH * (newlines + 1) + 8.0 * us));

  auto layout = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = pango_font_description_from_string("Inter");
  pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
  pango_font_description_set_absolute_size(desc, fontPx * PANGO_SCALE);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
  pango_layout_set_text(layout, text.c_str(), -1);
  pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

  int layoutW = 0, layoutH = 0;
  pango_layout_get_pixel_size(layout, &layoutW, &layoutH);

  m_width = std::max(m_width, layoutW + static_cast<int>(12.0 * us));
  m_height = std::max(m_height, layoutH + static_cast<int>(12.0 * us));

  cairo_save(cr);
  cairo_move_to(cr, 7.0 * us, 7.0 * us);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);

  cairo_move_to(cr, 6.0 * us, 6.0 * us);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.92);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);

  cairo_restore(cr);
}

}
