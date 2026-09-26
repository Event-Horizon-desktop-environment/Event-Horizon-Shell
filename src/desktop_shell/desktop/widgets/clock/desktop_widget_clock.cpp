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

#include "desktop_shell/shared/paint/glass_card_style.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"

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

void DesktopClockWidget::create() {}

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
      const std::chrono::time_zone* tzDb = nullptr;
      try {
        tzDb = std::chrono::locate_zone(tz);
      } catch (...) {
        tzDb = nullptr;
      }
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
  const double us = dock_ui_scale(sc.dock);
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const std::string text = build_clock_text(sc, m_format);

  std::string timeText = text;
  std::string dateText;
  if (const size_t nl = text.find('\n'); nl != std::string::npos) {
    timeText = text.substr(0, nl);
    dateText = text.substr(nl + 1);
  }

  const double timePx = static_cast<double>(m_fontSize > 0 ? m_fontSize : 64);
  const double datePx = timePx * 0.30;
  const double pad = 20.0 * us;

  auto make_line = [&](const std::string& s, const char* fd_str) {
    auto* l = pango_cairo_create_layout(cr);
    auto* fd = pango_font_description_from_string(fd_str);
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    pango_layout_set_text(l, s.c_str(), -1);
    return l;
  };
  char timeFd[64], dateFd[64];
  std::snprintf(timeFd, sizeof(timeFd), "Inter Light %.0f", timePx);
  std::snprintf(dateFd, sizeof(dateFd), "Inter %.0f", datePx);

  auto* timeLayout = make_line(timeText, timeFd);
  int timeW = 0, timeH = 0;
  pango_layout_get_pixel_size(timeLayout, &timeW, &timeH);
  int dateW = 0, dateH = 0;
  PangoLayout* dateLayout = nullptr;
  if (!dateText.empty()) {
    dateLayout = make_line(dateText, dateFd);
    pango_layout_get_pixel_size(dateLayout, &dateW, &dateH);
  }

  const double contentW = static_cast<double>(std::max(timeW, dateW));
  const double contentH = static_cast<double>(timeH) + (dateLayout ? (8.0 * us + dateH) : 0);
  m_width = static_cast<int>(std::ceil(contentW + pad * 2.0));
  m_height = static_cast<int>(std::ceil(contentH + pad * 2.0));

  cairo_save(cr);

  {
    eh::shell::shared::rounded_rect(cr, 0, 0, static_cast<double>(m_width),
                                    static_cast<double>(m_height), 26.0 * us);
    const double a = 0.92 * sc.appearance.overlayOpacityWidgetCard;
    cairo_set_source_rgba(cr, mc.dockFillR * 0.30, mc.dockFillG * 0.30, mc.dockFillB * 0.30, a);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  }

  auto show_shadowed = [&](PangoLayout* l, double x, double y, double r, double g, double b,
                           double a) {
    cairo_save(cr);
    cairo_move_to(cr, x + 1.0, y + 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.25);
    pango_cairo_show_layout(cr, l);
    cairo_restore(cr);
    cairo_move_to(cr, x, y);
    cairo_set_source_rgba(cr, r, g, b, a);
    pango_cairo_show_layout(cr, l);
  };

  show_shadowed(timeLayout, pad, pad, 1.0, 1.0, 1.0, 0.95);
  g_object_unref(timeLayout);
  if (dateLayout) {
    show_shadowed(dateLayout, pad, pad + timeH + 8.0 * us, 1.0, 1.0, 1.0, 0.62);
    g_object_unref(dateLayout);
  }

  cairo_restore(cr);
}

}
