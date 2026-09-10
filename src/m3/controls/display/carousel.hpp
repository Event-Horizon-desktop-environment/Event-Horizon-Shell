#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include <cairo/cairo.h>

#include "m3/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Carousel — horizontal scrollable container with snap points.
class Carousel {
public:
  Carousel() = default;

  void setItemCount(int n) { count_ = std::max(n, 0); }
  void setActiveIndex(int i) { activeIdx_ = std::clamp(i, 0, count_ - 1); }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }

  void paint(cairo_t* cr) {
    if (w_ <= 0 || h_ <= 0 || count_ <= 0) return;

    const float itemW = w_ * 0.7f;
    const float itemH = h_ * 0.85f;
    const float itemR = 12.0f;
    const float gap = 12.0f;
    const float visCenter = x_ + w_ * 0.5f;

    cairo_save(cr);

    // Clip to carousel bounds
    cairo_rectangle(cr, x_, y_, w_, h_);
    cairo_clip(cr);

    for (int i = 0; i < count_; ++i) {
      const float offset = (i - activeIdx_) * (itemW + gap);
      const float ix = visCenter + offset - itemW * 0.5f;

      // Only draw visible items
      if (ix + itemW < x_ || ix > x_ + w_) continue;

      const bool active = i == activeIdx_;
      const float scale = active ? 1.0f : 0.9f;
      const float sw = itemW * scale;
      const float sh = itemH * scale;
      const float sx = ix + (itemW - sw) * 0.5f;
      const float sy = y_ + (h_ - sh) * 0.5f;
      const float sr = itemR * scale;

      // Item surface
      cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, active ? 1.0f : 0.7f);
      cairo_new_path(cr);
      cairo_arc(cr, sx + sr, sy + sr, sr, M_PI, 1.5 * M_PI);
      cairo_arc(cr, sx + sw - sr, sy + sr, sr, 1.5 * M_PI, 2.0 * M_PI);
      cairo_arc(cr, sx + sw - sr, sy + sh - sr, sr, 0.0, 0.5 * M_PI);
      cairo_arc(cr, sx + sr, sy + sh - sr, sr, 0.5 * M_PI, M_PI);
      cairo_close_path(cr);
      cairo_fill(cr);
    }

    cairo_reset_clip(cr);
    cairo_restore(cr);

    // Page indicator dots
    const float dotR = 4.0f;
    const float dotGap = 12.0f;
    const float dotsW = (count_ - 1) * dotGap;
    const float dotsX = x_ + (w_ - dotsW) * 0.5f;
    const float dotsY = y_ + h_ - 12;

    for (int i = 0; i < count_; ++i) {
      const bool active = i == activeIdx_;
      cairo_save(cr);
      if (active) {
        cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 1.0f);
      } else {
        cairo_set_source_rgba(cr, textR_, textG_, textB_, 0.38f);
      }
      cairo_arc(cr, dotsX + i * dotGap, dotsY, dotR, 0, 2.0 * M_PI);
      cairo_fill(cr);
      cairo_restore(cr);
    }
  }

private:
  int count_ = 0;
  int activeIdx_ = 0;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 200;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
};

} // namespace m3
