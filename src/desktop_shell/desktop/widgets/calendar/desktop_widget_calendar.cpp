#include "desktop_shell/desktop/widgets/calendar/desktop_widget_calendar.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/dock/core/dock_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"

namespace eh::shell::desktop {
namespace {

bool same_day(const std::tm& a, const std::tm& b) {
   
  return a.tm_year == b.tm_year && a.tm_mon == b.tm_mon && a.tm_mday == b.tm_mday;
}

std::tm first_of_month(const std::tm& date) {
   
  std::tm d = date;
  d.tm_mday = 1;
  d.tm_hour = 0; d.tm_min = 0; d.tm_sec = 0;
  d.tm_isdst = -1;
  std::mktime(&d);
  return d;
}

std::tm start_of_week(const std::tm& d) {
   
  std::tm dt = d;
  dt.tm_hour = 0; dt.tm_min = 0; dt.tm_sec = 0;
  dt.tm_isdst = -1;
  std::mktime(&dt);
  int diff = dt.tm_wday;
  dt.tm_mday -= diff;
  std::mktime(&dt);
  return dt;
}

std::tm today_struct() {
   
  std::time_t t = std::time(nullptr);
  std::tm local{};
  localtime_r(&t, &local);
  return local;
}

using eh::shell::shared::rounded_rect;

double cell_x(int col, double us) {
   
  return 16.0 * us + static_cast<double>(col) * (39.0 * us + 2.5 * us);
}

double cell_y(int row, double us) {
   
  return (28.0 * us + 20.0 * us + 22.0 * us + 12.0 * us) + static_cast<double>(row) * 44.0 * us;
}

}

DesktopCalendarWidget::DesktopCalendarWidget() {
   
  m_displayDate = today_struct();
  m_selectedDate = m_displayDate;
}

DesktopCalendarWidget::~DesktopCalendarWidget() = default;

void DesktopCalendarWidget::create() {}

void DesktopCalendarWidget::prev_month() {
   
  m_displayDate.tm_mon -= 1;
  if (m_displayDate.tm_mon < 0) {
    m_displayDate.tm_mon = 11;
    m_displayDate.tm_year -= 1;
  }
  m_displayDate.tm_mday = 1;
  m_displayDate.tm_hour = 0; m_displayDate.tm_min = 0; m_displayDate.tm_sec = 0;
  m_displayDate.tm_isdst = -1;
  std::mktime(&m_displayDate);
}

void DesktopCalendarWidget::next_month() {
   
  m_displayDate.tm_mon += 1;
  if (m_displayDate.tm_mon > 11) {
    m_displayDate.tm_mon = 0;
    m_displayDate.tm_year += 1;
  }
  m_displayDate.tm_mday = 1;
  m_displayDate.tm_hour = 0; m_displayDate.tm_min = 0; m_displayDate.tm_sec = 0;
  m_displayDate.tm_isdst = -1;
  std::mktime(&m_displayDate);
}

bool DesktopCalendarWidget::on_click(double x, double y) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double W = static_cast<double>(intrinsicWidth());
  const double kPad = 16.0 * us;
  const double kHeaderFontSz = 20.0 * us;
  const double kNavBtnSz = 28.0 * us;
  const double kCellW = 39.0 * us;
  const double kCellR = 14.0 * us;
  const double kRowH = 44.0 * us;
  const double kTopPad = 28.0 * us;
  const double kYHeaderBase = kTopPad + kHeaderFontSz;
  const double navY = kYHeaderBase - kNavBtnSz / 2.0;
  const double prevX = W - kPad - kNavBtnSz * 2.0 - 4.0 * us;
  const double nextX = W - kPad - kNavBtnSz;

  // Check prev/next navigation buttons
  if (x >= prevX && x < prevX + kNavBtnSz && y >= navY && y < navY + kNavBtnSz) {
    prev_month();
    return true;
  }
  if (x >= nextX && x < nextX + kNavBtnSz && y >= navY && y < navY + kNavBtnSz) {
    next_month();
    return true;
  }

  // Check date cells for selection
  const int displayMonth = m_displayDate.tm_mon;
  const std::tm firstCell = start_of_week(first_of_month(m_displayDate));
  for (int row = 0; row < 6; ++row) {
    for (int col = 0; col < 7; ++col) {
      const int idx = row * 7 + col;
      std::tm dayDate = firstCell;
      dayDate.tm_mday += idx;
      dayDate.tm_hour = 0; dayDate.tm_min = 0; dayDate.tm_sec = 0;
      dayDate.tm_isdst = -1;
      std::mktime(&dayDate);

      if (dayDate.tm_mon != displayMonth) continue;

      const double cx = cell_x(col, us);
      const double cy = cell_y(row, us);
      const double centerX = cx + kCellW / 2.0;
      const double centerY = cy + kRowH / 2.0;

      if (std::abs(x - centerX) <= kCellR && std::abs(y - centerY) <= kCellR) {
        m_selectedDate = dayDate;
        return true;
      }
    }
  }

  return false;
}

void DesktopCalendarWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double W = static_cast<double>(intrinsicWidth());
  const double H = static_cast<double>(intrinsicHeight());

  const double kPad = 16.0 * us;
  const double kHeaderFontSz = 20.0 * us;
  const double kDayFontSz = 12.0 * us;
  const double kDateFontSz = 15.0 * us;
  const double kRowH = 44.0 * us;
  const double kCellW = 39.0 * us;
  const double kNavBtnSz = 28.0 * us;
  const double kCellR = 14.0 * us;
  const double kTopPad = 28.0 * us;
  const double kYHeaderBase = kTopPad + kHeaderFontSz;
  const double kYDayBase = kYHeaderBase + 22.0 * us;
  const double kYGridTop = kYDayBase + 12.0 * us;

  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);

  cairo_save(cr);

  eh::shell::shared::paint_glass_card(cr, 0, 0, W, H, 18.0 * us, mc,
                                      sc.appearance.overlayOpacityWidgetCard);

  char dateStr[64];
  std::strftime(dateStr, sizeof(dateStr), "%B %Y", &m_displayDate);

  cairo_set_source_rgba(cr, 0.96, 0.98, 0.99, 0.92);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, kHeaderFontSz);
  cairo_move_to(cr, kPad, kYHeaderBase);
  cairo_show_text(cr, dateStr);

  const double navY = kYHeaderBase - kNavBtnSz / 2.0;
  const double prevX = W - kPad - kNavBtnSz * 2.0 - 4.0 * us;
  const double nextX = W - kPad - kNavBtnSz;

  eh::shell::draw_material_glyph(cr, prevX + kNavBtnSz / 2.0, navY + kNavBtnSz / 2.0, 18.0 * us,
                                  "chevron_left", mc.outlineR, mc.outlineG, mc.outlineB, 0.65);
  eh::shell::draw_material_glyph(cr, nextX + kNavBtnSz / 2.0, navY + kNavBtnSz / 2.0, 18.0 * us,
                                  "chevron_right", mc.outlineR, mc.outlineG, mc.outlineB, 0.65);

  static const char* kDayLabels[] = {"S", "M", "T", "W", "T", "F", "S"};
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, kDayFontSz);
  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.60);

  for (int col = 0; col < 7; ++col) {
    const double cx = cell_x(col, us);
    cairo_text_extents_t te;
    cairo_text_extents(cr, kDayLabels[col], &te);
    cairo_move_to(cr, cx + (kCellW - te.x_advance) / 2.0, kYDayBase);
    cairo_show_text(cr, kDayLabels[col]);
  }

  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.10);
  cairo_set_line_width(cr, 1.0);
  for (int row = 0; row <= 6; ++row) {
    const double y = kYGridTop + static_cast<double>(row) * kRowH;
    cairo_move_to(cr, kPad + 4.0 * us, y);
    cairo_line_to(cr, W - kPad - 4.0 * us, y);
    cairo_stroke(cr);
  }

  const std::tm firstCell = start_of_week(first_of_month(m_displayDate));
  const int displayMonth = m_displayDate.tm_mon;
  const std::tm today = today_struct();

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
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
      const bool isToday = same_day(dayDate, today);
      const bool isSelected = same_day(dayDate, m_selectedDate);

      const double cx = cell_x(col, us);
      const double cy = cell_y(row, us);
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
        cairo_arc(cr, centerX, centerY + 10.0 * us, 2.5 * us, 0, 2 * M_PI);
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

  cairo_restore(cr);
}

}
