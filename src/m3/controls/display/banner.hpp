#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <cairo/cairo.h>

#include "m3/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Banner — persistent message with optional action.
class Banner {
public:
  Banner() = default;

  void setText(std::string_view t) { text_ = t; }
  void setAction(std::string_view a) { action_ = a; }
  void setVisible(bool v) { visible_ = v; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }

  [[nodiscard]] bool visible() const noexcept { return visible_; }

  void paint(cairo_t* cr) {
    if (!visible_ || w_ <= 0) return;

    cairo_save(cr);

    // Container
    cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 1.0f);
    cairo_rectangle(cr, x_, y_, w_, h_);
    cairo_fill(cr);

    // Bottom edge
    cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.2f);
    cairo_rectangle(cr, x_, y_ + h_ - 1, w_, 1);
    cairo_fill(cr);

    // Text
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14);
    cairo_set_source_rgba(cr, textR_, textG_, textB_, 0.93f);
    cairo_move_to(cr, x_ + 16, y_ + 24);
    cairo_show_text(cr, text_.c_str());

    // Action
    if (!action_.empty()) {
      cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 1.0f);
      cairo_set_font_size(cr, 14);
      cairo_text_extents_t te;
      cairo_text_extents(cr, action_.c_str(), &te);
      cairo_move_to(cr, x_ + w_ - te.width - 16, y_ + h_ - 12);
      cairo_show_text(cr, action_.c_str());
    }

    cairo_restore(cr);
  }

private:
  std::string text_;
  std::string action_;
  bool visible_ = false;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 72;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
};

} // namespace m3
