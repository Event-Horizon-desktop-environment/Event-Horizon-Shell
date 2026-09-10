#pragma once

#include <cstdint>

#include <cairo/cairo.h>

namespace m3 {

// Horizontal or vertical divider line.
class Separator {
public:
  enum class Orientation { Horizontal, Vertical };

  Separator() = default;

  void setOrientation(Orientation o) { orientation_ = o; }
  void setColor(float r, float g, float b, float a = 1.0f) { cr_ = r; cg_ = g; cb_ = b; ca_ = a; }
  void setThickness(float t) { thickness_ = t; }
  void setLength(float l) { length_ = l; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }

  void paint(cairo_t* cr) const {
    if (ca_ <= 0) return;

    cairo_save(cr);
    cairo_set_source_rgba(cr, cr_, cg_, cb_, ca_);
    cairo_set_line_width(cr, thickness_);

    if (orientation_ == Orientation::Horizontal) {
      const float cy = y_ + h_ * 0.5f;
      cairo_move_to(cr, x_, cy);
      cairo_line_to(cr, x_ + (length_ > 0 ? length_ : w_), cy);
    } else {
      const float cx = x_ + w_ * 0.5f;
      cairo_move_to(cr, cx, y_);
      cairo_line_to(cr, cx, y_ + (length_ > 0 ? length_ : h_));
    }

    cairo_stroke(cr);
    cairo_restore(cr);
  }

private:
  Orientation orientation_ = Orientation::Horizontal;
  float thickness_ = 1.0f;
  float length_ = 0.0f;
  float cr_ = 0.35f, cg_ = 0.35f, cb_ = 0.37f, ca_ = 1.0f;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
};

} // namespace m3
