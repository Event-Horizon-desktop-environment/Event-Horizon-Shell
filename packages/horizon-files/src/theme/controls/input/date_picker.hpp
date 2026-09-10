#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

#include <cairo/cairo.h>

#include "theme/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Date Picker — simple calendar grid with month navigation.
class DatePicker {
public:
  DatePicker() = default;

  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }
  void setDay(int d) { day_ = std::clamp(d, 1, 31); }
  void setMonth(int m) { month_ = std::clamp(m, 1, 12); }
  void setYear(int y) { year_ = y; }

  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }

  void paint(cairo_t* cr) {
    if (w_ <= 0 || h_ <= 0) return;

    const float pad = 16.0f;
    const float cellH = 36.0f;
    const float cellW = (w_ - pad * 2) / 7.0f;
    const float headerH = 48.0f;
    const float r = 8.0f;

    cairo_save(cr);

    // Container
    cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 1.0f);
    cairo_rectangle(cr, x_, y_, w_, h_);
    cairo_fill(cr);

    // Month/year header
    cairo_set_source_rgba(cr, textR_, textG_, textB_, 0.93f);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 18);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d/%d", month_, year_);
    cairo_move_to(cr, x_ + pad, y_ + 28);
    cairo_show_text(cr, buf);

    // Day names
    const char* days[] = {"S", "M", "T", "W", "T", "F", "S"};
    cairo_set_font_size(cr, 12);
    for (int i = 0; i < 7; ++i) {
      cairo_set_source_rgba(cr, textR_, textG_, textB_, 0.46f);
      const float dx = x_ + pad + i * cellW + (cellW - 6) * 0.5f;
      cairo_move_to(cr, dx, y_ + headerH);
      cairo_show_text(cr, days[i]);
    }

    // Day cells
    const float gridY = y_ + headerH + 8;
    for (int d = 1; d <= 28; ++d) {
      const int row = (d - 1) / 7;
      const int col = (d - 1) % 7;
      const float dx = x_ + pad + col * cellW;
      const float dy = gridY + row * cellH;

      if (d == day_) {
        cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 1.0f);
        cairo_arc(cr, dx + cellW * 0.5f, dy + cellH * 0.5f, cellH * 0.4f, 0, 2.0 * M_PI);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 1);
      } else {
        cairo_set_source_rgba(cr, textR_, textG_, textB_, 0.66f);
      }

      cairo_set_font_size(cr, 14);
      char dbuf[4];
      std::snprintf(dbuf, sizeof(dbuf), "%d", d);
      cairo_move_to(cr, dx + cellW * 0.5f - 5, dy + cellH * 0.5f + 5);
      cairo_show_text(cr, dbuf);
    }

    cairo_restore(cr);
  }

private:
  int day_ = 1, month_ = 1, year_ = 2025;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 360;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
};

} // namespace m3
