#pragma once

// Shared month-grid card paint, single source for the desktop calendar widget
// and the dock/taskbar calendar popup. Same code, same pixels — the two
// surfaces cannot drift apart.
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <cairo/cairo.h>

namespace eh::shell::desktop {

inline bool cal_same_day(const std::tm& a, const std::tm& b) {
  return a.tm_year == b.tm_year && a.tm_mon == b.tm_mon && a.tm_mday == b.tm_mday;
}

inline std::tm cal_first_of_month(const std::tm& date) {
  std::tm d = date;
  d.tm_mday = 1;
  d.tm_hour = 0; d.tm_min = 0; d.tm_sec = 0;
  d.tm_isdst = -1;
  std::mktime(&d);
  return d;
}

inline std::tm cal_start_of_week(const std::tm& d) {
  std::tm dt = d;
  dt.tm_hour = 0; dt.tm_min = 0; dt.tm_sec = 0;
  dt.tm_isdst = -1;
  std::mktime(&dt);
  int diff = dt.tm_wday;
  dt.tm_mday -= diff;
  std::mktime(&dt);
  return dt;
}

inline std::tm cal_today_struct() {
  std::time_t t = std::time(nullptr);
  std::tm local{};
  localtime_r(&t, &local);
  return local;
}

inline double cal_cell_x(int col, double us) {
  return 16.0 * us + static_cast<double>(col) * (39.0 * us + 2.5 * us);
}

inline double cal_cell_y(int row, double us) {
  return (28.0 * us + 20.0 * us + 22.0 * us + 12.0 * us) + static_cast<double>(row) * 44.0 * us;
}

struct CalendarNavGeometry {
  double prevX = 0;
  double nextX = 0;
  double navY = 0;
  double btnSz = 0;
};

inline CalendarNavGeometry cal_nav_geom(double W, double us) {
  CalendarNavGeometry g;
  const double kPad = 16.0 * us;
  g.btnSz = 28.0 * us;
  g.navY = (28.0 * us + 20.0 * us) - g.btnSz / 2.0;
  g.prevX = W - kPad - g.btnSz * 2.0 - 4.0 * us;
  g.nextX = W - kPad - g.btnSz;
  return g;
}

inline bool cal_cell_hit(int row, int col, double x, double y, double us) {
  const double cx = cal_cell_x(col, us) + (39.0 * us) / 2.0;
  const double cy = cal_cell_y(row, us) + (44.0 * us) / 2.0;
  const double dx = x - cx;
  const double dy = y - cy;
  const double r = 14.0 * us;
  return dx * dx + dy * dy <= r * r;
}

// Paints the full card at (0,0)-(W,H). `us` is the UI scale (desktop honors
// the dock scale; popups use 1.0). Cell geometry is fixed so hit-testing in
// both hosts stays in sync with these pixels.
inline void paint_calendar_card(cairo_t* cr, double W, double H, double us,
                                const eh::config::ChromePaintColors& mc, double cardAlpha,
                                const std::tm& displayDate, const std::tm& selectedDate) {
  const double kPad = 16.0 * us;
  const double kHeaderFontSz = 20.0 * us;
  const double kDayFontSz = 12.0 * us;
  const double kDateFontSz = 15.0 * us;
  const double kRowH = 44.0 * us;
  const double kCellW = 39.0 * us;
  const double kCellR = 14.0 * us;
  const double kTopPad = 28.0 * us;
  const double kYHeaderBase = kTopPad + kHeaderFontSz;
  const double kYDayBase = kYHeaderBase + 22.0 * us;

  cairo_save(cr);

  // Dark glass card, single fill + single hairline border so the corners stay
  // crisp (m3::Box paints 3 strokes + a shadow, which rounds them visibly).
  eh::shell::shared::rounded_rect(cr, 0, 0, W, H, 24.0 * us);
  cairo_set_source_rgba(cr, mc.dockFillR * 0.30, mc.dockFillG * 0.30, mc.dockFillB * 0.30,
                        0.92 * cardAlpha);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  char monthStr[32], yearStr[16];
  std::strftime(monthStr, sizeof(monthStr), "%B", &displayDate);
  std::strftime(yearStr, sizeof(yearStr), "%Y", &displayDate);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, kHeaderFontSz);
  cairo_text_extents_t teMonth;
  cairo_text_extents(cr, monthStr, &teMonth);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
  cairo_move_to(cr, kPad, kYHeaderBase);
  cairo_show_text(cr, monthStr);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.50);
  cairo_move_to(cr, kPad + teMonth.x_advance + 8.0 * us, kYHeaderBase);
  cairo_show_text(cr, yearStr);

  const CalendarNavGeometry nav = cal_nav_geom(W, us);

  eh::shell::draw_material_glyph(cr, nav.prevX + nav.btnSz / 2.0, nav.navY + nav.btnSz / 2.0, 18.0 * us,
                                  "chevron_left", 1.0, 1.0, 1.0, 0.60);
  eh::shell::draw_material_glyph(cr, nav.nextX + nav.btnSz / 2.0, nav.navY + nav.btnSz / 2.0, 18.0 * us,
                                  "chevron_right", 1.0, 1.0, 1.0, 0.60);

  static const char* kDayLabels[] = {"S", "M", "T", "W", "T", "F", "S"};
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, kDayFontSz);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.50);

  for (int col = 0; col < 7; ++col) {
    const double cx = cal_cell_x(col, us);
    cairo_text_extents_t te;
    cairo_text_extents(cr, kDayLabels[col], &te);
    cairo_move_to(cr, cx + (kCellW - te.x_advance) / 2.0, kYDayBase);
    cairo_show_text(cr, kDayLabels[col]);
  }

  // NOTE: grid lines removed for the clean look. Cell geometry is fixed so
  // on_click hit-testing stays in sync.

  const std::tm firstCell = cal_start_of_week(cal_first_of_month(displayDate));
  const int displayMonth = displayDate.tm_mon;

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, kDateFontSz);

  const std::tm today = cal_today_struct();
  // Week band: soft wash behind whichever row holds today.
  {
    for (int row = 0; row < 6; ++row) {
      bool holdsToday = false;
      for (int col = 0; col < 7; ++col) {
        std::tm d = firstCell;
        d.tm_mday += row * 7 + col;
        d.tm_hour = 0; d.tm_min = 0; d.tm_sec = 0;
        d.tm_isdst = -1;
        std::mktime(&d);
        if (cal_same_day(d, today)) { holdsToday = true; break; }
      }
      if (holdsToday) {
        const double bandY = cal_cell_y(row, us);
        eh::shell::shared::rounded_rect(cr, kPad, bandY, W - kPad * 2.0, kRowH, 12.0 * us);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.05);
        cairo_fill(cr);
        break;
      }
    }
  }

  for (int row = 0; row < 6; ++row) {
    for (int col = 0; col < 7; ++col) {
      const int idx = row * 7 + col;
      std::tm dayDate = firstCell;
      dayDate.tm_mday += idx;
      dayDate.tm_hour = 0; dayDate.tm_min = 0; dayDate.tm_sec = 0;
      dayDate.tm_isdst = -1;
      std::mktime(&dayDate);

      const bool isCurrentMonth = dayDate.tm_mon == displayMonth;
      const bool isToday = cal_same_day(dayDate, today);
      const bool isSelected = cal_same_day(dayDate, selectedDate);

      const double cx = cal_cell_x(col, us);
      const double cy = cal_cell_y(row, us);
      const double centerX = cx + kCellW / 2.0;
      const double centerY = cy + kRowH / 2.0;

      if (isToday) {
        // Today owns the accent: filled circle, white numeral.
        cairo_new_path(cr);
        cairo_arc(cr, centerX, centerY, kCellR, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.90);
        cairo_fill(cr);
      } else if (isSelected) {
        // Selected day: quiet glass wash + ring, never fights today.
        eh::shell::shared::rounded_rect(cr, cx + 2.0 * us, cy + 5.0 * us,
                                        kCellW - 4.0 * us, kRowH - 10.0 * us, 10.0 * us);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.35);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
      }

      char dayBuf[8];
      std::snprintf(dayBuf, sizeof(dayBuf), "%d", dayDate.tm_mday);
      cairo_text_extents_t te;
      cairo_text_extents(cr, dayBuf, &te);

      if (isSelected || isToday) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
      } else if (isCurrentMonth) {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.90);
      } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.28);
      }

      cairo_move_to(cr, centerX - te.x_advance / 2.0, centerY - te.y_bearing - te.height * 0.5);
      cairo_show_text(cr, dayBuf);
    }
  }

  cairo_restore(cr);
}

} // namespace eh::shell::desktop
