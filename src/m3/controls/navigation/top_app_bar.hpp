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

// M3 Top App Bar — Small, Center, Medium, Large.
class TopAppBar {
public:
  enum class Variant { Small, Center, Medium, Large };

  TopAppBar() = default;

  void setVariant(Variant v) { variant_ = v; }
  void setTitle(std::string_view t) { title_ = t; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }

  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }

  void paint(cairo_t* cr) {
    if (w_ <= 0 || h_ <= 0) return;

    const bool large = variant_ == Variant::Large;
    const bool medium = variant_ == Variant::Medium;
    const bool center = variant_ == Variant::Center;

    // Background
    cairo_save(cr);
    cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 1.0f);
    cairo_rectangle(cr, x_, y_, w_, h_);
    cairo_fill(cr);

    // Bottom edge shadow
    cairo_set_source_rgba(cr, 0, 0, 0, 0.06f);
    cairo_rectangle(cr, x_, y_ + h_ - 1, w_, 1);
    cairo_fill(cr);
    cairo_restore(cr);

    // Title
    cairo_save(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

    float ty;
    float fontSize;
    if (large) {
      fontSize = 28.0f;
      ty = y_ + h_ - 28.0f;
    } else if (medium) {
      fontSize = 22.0f;
      ty = y_ + h_ - 28.0f;
    } else {
      fontSize = 20.0f;
      ty = y_ + (h_ - fontSize) * 0.5f + fontSize * 0.75f;
    }

    cairo_set_font_size(cr, fontSize);
    cairo_set_source_rgba(cr, textR_, textG_, textB_, 0.93f);

    float tx;
    if (center) {
      cairo_text_extents_t te;
      cairo_text_extents(cr, title_.c_str(), &te);
      tx = x_ + (w_ - te.width) * 0.5f;
    } else {
      tx = x_ + 16.0f;
    }

    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, title_.c_str());
    cairo_restore(cr);
  }

private:
  Variant variant_ = Variant::Small;
  std::string title_;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 64;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
};

} // namespace m3
