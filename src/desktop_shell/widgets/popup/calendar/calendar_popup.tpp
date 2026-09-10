#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "m3/core/primitives/box.hpp"
#include <cairo/cairo.h>

namespace eh::shell::dock::popup::calendar {
namespace {

constexpr double kPad = 16.0;
constexpr double kHeaderFontSz = 20.0;
constexpr double kDayFontSz = 12.0;
constexpr double kDateFontSz = 15.0;
constexpr double kRowH = 44.0;
constexpr double kCellW = 39.0;
constexpr double kCellGap = 2.5;
constexpr double kNavBtnSz = 28.0;
constexpr double kCellR = 14.0;

constexpr double kTopPad = 28.0;
constexpr double kYHeaderBase = kTopPad + kHeaderFontSz;
constexpr double kYDayBase = kYHeaderBase + 22.0;
constexpr double kYGridTop = kYDayBase + 12.0;

bool same_day(const std::tm& a, const std::tm& b) {
  return a.tm_year == b.tm_year && a.tm_mon == b.tm_mon && a.tm_mday == b.tm_mday;
}

[[maybe_unused]] std::tm first_of_month(const std::tm& date) {
  std::tm d = date;
  d.tm_mday = 1;
  d.tm_hour = 0; d.tm_min = 0; d.tm_sec = 0;
  d.tm_isdst = -1;
  std::mktime(&d);
  return d;
}

[[maybe_unused]] std::tm start_of_week(const std::tm& d) {
  std::tm dt = d;
  dt.tm_hour = 0; dt.tm_min = 0; dt.tm_sec = 0;
  dt.tm_isdst = -1;
  std::mktime(&dt);
  int diff = dt.tm_wday;
  dt.tm_mday -= diff;
  std::mktime(&dt);
  return dt;
}

void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  const double rad = std::min({r, w * 0.5, h * 0.5});
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - rad, y + rad, rad, -M_PI_2, 0);
  cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, M_PI_2);
  cairo_arc(cr, x + rad, y + h - rad, rad, M_PI_2, M_PI);
  cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

double cell_x(int col) {
  return kPad + static_cast<double>(col) * (kCellW + kCellGap);
}

double cell_y(int row) {
  return kYGridTop + static_cast<double>(row) * kRowH;
}

const std::tm& today_tm() {
  static std::tm cached = []() {
    std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_r(&t, &local);
    return local;
  }();
  return cached;
}

}

template<typename A>
inline void dock_calendar_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  const double W = static_cast<double>(kCalendarPopupW);
  const double H = static_cast<double>(kCalendarPopupH);

  const double shellOv = static_cast<double>(
      eh::config::overlay_surface_alpha_scale(sc, eh::config::OverlaySurfaceAlphaKind::Calendar));
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);

  constexpr double kShadowOffX = 2.0, kShadowOffY = 3.0, kShadowAlpha = 0.22;
  cairo_save(cr);
  rounded_rect(cr, kShadowOffX, kShadowOffY, W, H, 18.0);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, kShadowAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  {
    m3::Box box;
    box.setColor(static_cast<float>(mc.dockFillR * 0.35), static_cast<float>(mc.dockFillG * 0.35),
                 static_cast<float>(mc.dockFillB * 0.35), static_cast<float>(0.78 * shellOv));
    box.setRadius(18.0f);
    box.setGeometry(0, 0, static_cast<float>(W), static_cast<float>(H));
    box.setGlassy(true);
    box.paint(cr);
  }

  rounded_rect(cr, 0.5, 0.5, W - 1.0, H - 1.0, 18.0);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  char dateStr[64];
  std::strftime(dateStr, sizeof(dateStr), "%B %Y", &app.calDisplayDate);

  cairo_set_source_rgba(cr, 0.96, 0.98, 0.99, 0.92);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, kHeaderFontSz);
  cairo_move_to(cr, kPad, kYHeaderBase);
  cairo_show_text(cr, dateStr);

  const double navY = kYHeaderBase - kNavBtnSz / 2.0;
  const double prevX = W - kPad - kNavBtnSz * 2.0 - 4.0;
  const double nextX = W - kPad - kNavBtnSz;

  eh::shell::draw_material_glyph(cr, prevX + kNavBtnSz / 2.0, navY + kNavBtnSz / 2.0, 18.0,
                                  "chevron_left", mc.outlineR, mc.outlineG, mc.outlineB, 0.65);
  eh::shell::draw_material_glyph(cr, nextX + kNavBtnSz / 2.0, navY + kNavBtnSz / 2.0, 18.0,
                                  "chevron_right", mc.outlineR, mc.outlineG, mc.outlineB, 0.65);

  static const char* kDayLabels[] = {"S", "M", "T", "W", "T", "F", "S"};
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, kDayFontSz);
  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.60);

  for (int col = 0; col < 7; ++col) {
    const double cx = cell_x(col);
    cairo_text_extents_t te;
    cairo_text_extents(cr, kDayLabels[col], &te);
    cairo_move_to(cr, cx + (kCellW - te.x_advance) / 2.0, kYDayBase);
    cairo_show_text(cr, kDayLabels[col]);
  }

  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.10);
  cairo_set_line_width(cr, 1.0);
  for (int row = 0; row <= 6; ++row) {
    const double y = kYGridTop + static_cast<double>(row) * kRowH;
    cairo_move_to(cr, kPad + 4.0, y);
    cairo_line_to(cr, W - kPad - 4.0, y);
    cairo_stroke(cr);
  }

  const std::tm firstCell = start_of_week(first_of_month(app.calDisplayDate));
  const int displayMonth = app.calDisplayDate.tm_mon;

  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, kDateFontSz);

  for (int row = 0; row < 6; ++row) {
    for (int col = 0; col < 7; ++col) {
      const int idx = row * 7 + col;
      std::tm dayDate = firstCell;
      dayDate.tm_mday += idx;
      dayDate.tm_hour = 0; dayDate.tm_min = 0; dayDate.tm_sec = 0;
      dayDate.tm_isdst = -1;
      std::mktime(&dayDate);

      const bool isCurrentMonth = dayDate.tm_mon == displayMonth;
      const bool isToday = same_day(dayDate, today_tm());
      const bool isSelected = same_day(dayDate, app.calSelectedDate);

      const double cx = cell_x(col);
      const double cy = cell_y(row);
      const double centerX = cx + kCellW / 2.0;
      const double centerY = cy + kRowH / 2.0;

      if (isSelected) {
        cairo_new_path(cr);
        cairo_arc(cr, centerX, centerY, kCellR, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.75);
        cairo_fill(cr);
      }

      if (isToday && !isSelected) {
        cairo_new_path(cr);
        cairo_arc(cr, centerX, centerY + 10.0, 2.5, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.70);
        cairo_fill(cr);
      }

      char dayBuf[8];
      std::snprintf(dayBuf, sizeof(dayBuf), "%d", dayDate.tm_mday);
      cairo_text_extents_t te;
      cairo_text_extents(cr, dayBuf, &te);

      if (isSelected) {
        cairo_set_source_rgba(cr, 0.08, 0.10, 0.12, 0.92);
      } else if (isToday) {
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.92);
      } else if (isCurrentMonth) {
        cairo_set_source_rgba(cr, 0.92, 0.94, 0.96, 0.85);
      } else {
        cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.30);
      }

      cairo_move_to(cr, centerX - te.x_advance / 2.0, centerY - te.y_bearing - te.height * 0.5);
      cairo_show_text(cr, dayBuf);
    }
  }
}

}
