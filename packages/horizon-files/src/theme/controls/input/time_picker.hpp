#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

#include <cairo/cairo.h>

#include "theme/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Time Picker — simple clock-face + digital display.
class TimePicker {
public:
  TimePicker() = default;

  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }
  void setHour(int h) { hour_ = std::clamp(h, 1, 12); }
  void setMinute(int m) { minute_ = std::clamp(m, 0, 59); }

  void paint(cairo_t* cr) {
    if (w_ <= 0 || h_ <= 0) return;

    const float cx = x_ + w_ * 0.5f;
    const float cy = y_ + h_ * 0.5f;
    const float faceR = std::min(w_, h_) * 0.35f;

    cairo_save(cr);

    // Clock face
    cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 1.0f);
    cairo_arc(cr, cx, cy, faceR, 0, 2.0 * M_PI);
    cairo_fill(cr);

    // Tick marks
    cairo_set_source_rgba(cr, textR_, textG_, textB_, 0.3f);
    cairo_set_line_width(cr, 1);
    for (int i = 0; i < 12; ++i) {
      const float a = i * M_PI / 6.0f - M_PI / 2.0f;
      const float r1 = faceR * 0.85f;
      const float r2 = faceR * 0.92f;
      cairo_move_to(cr, cx + r1 * std::cos(a), cy + r1 * std::sin(a));
      cairo_line_to(cr, cx + r2 * std::cos(a), cy + r2 * std::sin(a));
      cairo_stroke(cr);
    }

    // Hour hand
    const float ha = (hour_ % 12) * M_PI / 6.0f - M_PI / 2.0f;
    cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 1.0f);
    cairo_set_line_width(cr, 3);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx + faceR * 0.5f * std::cos(ha), cy + faceR * 0.5f * std::sin(ha));
    cairo_stroke(cr);

    // Minute hand
    const float ma = minute_ * M_PI / 30.0f - M_PI / 2.0f;
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx + faceR * 0.7f * std::cos(ma), cy + faceR * 0.7f * std::sin(ma));
    cairo_stroke(cr);

    // Center dot
    cairo_arc(cr, cx, cy, 4, 0, 2.0 * M_PI);
    cairo_fill(cr);

    // Digital display
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);
    cairo_set_source_rgba(cr, textR_, textG_, textB_, 0.93f);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", hour_, minute_);
    cairo_move_to(cr, cx - 28, y_ + h_ - 20);
    cairo_show_text(cr, buf);

    cairo_restore(cr);
  }

private:
  int hour_ = 12, minute_ = 0;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 300;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
};

} // namespace m3
