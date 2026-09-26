#include "desktop_shell/desktop/widgets/calendar/desktop_widget_calendar.hpp"
#include "desktop_shell/desktop/widgets/calendar/calendar_card_paint.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/dock/core/dock_app.h"

#include <ctime>
#include <ctime>

namespace eh::shell::desktop {

DesktopCalendarWidget::DesktopCalendarWidget() {
  m_displayDate = cal_today_struct();
  m_selectedDate = m_displayDate;
}

DesktopCalendarWidget::~DesktopCalendarWidget() = default;

void DesktopCalendarWidget::create() {}

void DesktopCalendarWidget::shift_month(int delta) {
  m_displayDate.tm_mon += delta;
  if (m_displayDate.tm_mon < 0) {
    m_displayDate.tm_mon = 11;
    m_displayDate.tm_year -= 1;
  } else if (m_displayDate.tm_mon > 11) {
    m_displayDate.tm_mon = 0;
    m_displayDate.tm_year += 1;
  }
  m_displayDate.tm_mday = 1;
  m_displayDate.tm_hour = 0; m_displayDate.tm_min = 0; m_displayDate.tm_sec = 0;
  m_displayDate.tm_isdst = -1;
  std::mktime(&m_displayDate);
}

void DesktopCalendarWidget::prev_month() { shift_month(-1); }

void DesktopCalendarWidget::next_month() { shift_month(1); }

bool DesktopCalendarWidget::on_click(double x, double y) {
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double W = static_cast<double>(intrinsicWidth());
  const CalendarNavGeometry nav = cal_nav_geom(W, us);

  if (x >= nav.prevX && x < nav.prevX + nav.btnSz && y >= nav.navY && y < nav.navY + nav.btnSz) {
    prev_month();
    return true;
  }
  if (x >= nav.nextX && x < nav.nextX + nav.btnSz && y >= nav.navY && y < nav.navY + nav.btnSz) {
    next_month();
    return true;
  }

  const int displayMonth = m_displayDate.tm_mon;
  const std::tm firstCell = cal_start_of_week(cal_first_of_month(m_displayDate));
  for (int row = 0; row < 6; ++row) {
    for (int col = 0; col < 7; ++col) {
      const int idx = row * 7 + col;
      std::tm dayDate = firstCell;
      dayDate.tm_mday += idx;
      dayDate.tm_hour = 0; dayDate.tm_min = 0; dayDate.tm_sec = 0;
      dayDate.tm_isdst = -1;
      std::mktime(&dayDate);

      if (dayDate.tm_mon != displayMonth) continue;

      if (cal_cell_hit(row, col, x, y, us)) {
        m_selectedDate = dayDate;
        return true;
      }
    }
  }

  return false;
}

void DesktopCalendarWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
  const double us = dock_ui_scale(sc.dock);
  const double W = static_cast<double>(intrinsicWidth());
  const double H = static_cast<double>(intrinsicHeight());
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  paint_calendar_card(cr, W, H, us, mc, sc.appearance.overlayOpacityWidgetCard,
                      m_displayDate, m_selectedDate);
}

}
