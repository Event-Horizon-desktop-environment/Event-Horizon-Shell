#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <cairo/cairo.h>

#include "m3/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Bottom App Bar — container with FAB notch, action icon slots.
class BottomAppBar {
public:
  BottomAppBar() = default;

  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }
  void setFabDiameter(float d) { fabD_ = d; }

  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }

  void paint(cairo_t* cr) {
    if (w_ <= 0 || h_ <= 0) return;

    cairo_save(cr);

    // Surface
    cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 1.0f);
    cairo_new_path(cr);
    cairo_rectangle(cr, x_, y_, w_, h_);
    cairo_fill(cr);

    // Top edge shadow
    cairo_set_source_rgba(cr, 0, 0, 0, 0.08f);
    cairo_rectangle(cr, x_, y_, w_, 1);
    cairo_fill(cr);

    cairo_restore(cr);
  }

private:
  float x_ = 0, y_ = 0, w_ = 0, h_ = 80;
  float fabD_ = 56.0f;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
};

} // namespace m3
