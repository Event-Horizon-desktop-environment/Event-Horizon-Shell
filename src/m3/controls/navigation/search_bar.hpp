#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <cairo/cairo.h>

#include "m3/core/primitives/box.hpp"
#include "m3/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Search Bar — input-style bar with leading icon and optional trailing action.
class SearchBar {
public:
  SearchBar() = default;

  void setText(std::string_view t) { text_ = t; }
  void setHint(std::string_view h) { hint_ = h; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }
  void setFocused(bool f) { focused_ = f; }
  void setOnSubmit(std::function<void(std::string)> cb) { onSubmit_ = std::move(cb); }

  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }

  bool containsPoint(float px, float py) const noexcept {
    return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
  }

  void paint(cairo_t* cr) {
    if (w_ <= 0 || h_ <= 0) return;

    const float r = h_ * 0.5f;
    const float iconSize = 20.0f;

    cairo_save(cr);

    // Container (Box + glassy for the frosted inner rim effect, radius = pill (h/2) for full conformance)
    Box bgBox;
    bgBox.setColor(surfaceR_, surfaceG_, surfaceB_, 1.0f);
    bgBox.setRadius(r);
    bgBox.setGeometry(x_, y_, w_, h_);
    bgBox.setGlassy(true);
    bgBox.paint(cr);

    // Outline on focus
    if (focused_) {
      cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 1.0f);
      cairo_set_line_width(cr, 2);
      cairo_new_path(cr);
      cairo_arc(cr, x_ + r, y_ + r, r, M_PI, 1.5 * M_PI);
      cairo_arc(cr, x_ + w_ - r, y_ + r, r, 1.5 * M_PI, 2.0 * M_PI);
      cairo_arc(cr, x_ + w_ - r, y_ + h_ - r, r, 0.0, 0.5 * M_PI);
      cairo_arc(cr, x_ + r, y_ + h_ - r, r, 0.5 * M_PI, M_PI);
      cairo_close_path(cr);
      cairo_stroke(cr);
    }

    // Leading search icon
    cairo_select_font_face(cr, "Symbols Nerd Font", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, iconSize);
    cairo_set_source_rgba(cr, textR_, textG_, textB_, 0.46f);
    cairo_move_to(cr, x_ + 16, y_ + h_ * 0.5f + iconSize * 0.3f);
    cairo_show_text(cr, "\uD83D\uDD0D");  // magnifying glass

    // Text or hint
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 16);
    if (text_.empty()) {
      cairo_set_source_rgba(cr, textR_, textG_, textB_, 0.38f);
      cairo_move_to(cr, x_ + 48, y_ + h_ * 0.5f + 6);
      cairo_show_text(cr, hint_.empty() ? "Search" : hint_.c_str());
    } else {
      cairo_set_source_rgba(cr, textR_, textG_, textB_, 0.93f);
      cairo_move_to(cr, x_ + 48, y_ + h_ * 0.5f + 6);
      cairo_show_text(cr, text_.c_str());
    }

    cairo_restore(cr);
  }

private:
  std::string text_;
  std::string hint_;
  bool focused_ = false;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 48;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
  std::function<void(std::string)> onSubmit_;
};

} // namespace m3
