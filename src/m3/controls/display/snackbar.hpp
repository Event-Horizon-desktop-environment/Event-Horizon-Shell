#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <cairo/cairo.h>

#include "m3/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Snackbar — brief message at the bottom with optional action.
class Snackbar {
public:
  Snackbar() = default;

  void setText(std::string_view t) { text_ = t; }
  void setAction(std::string_view a) { action_ = a; }
  void setVisible(bool v) { visible_ = v; }
  void setGeometry(float x, float y, float w, float h) { sx_ = x; sy_ = y; sw_ = w; sh_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }
  void setOnAction(std::function<void()> cb) { onAction_ = std::move(cb); }

  [[nodiscard]] bool visible() const noexcept { return visible_; }

  void paint(cairo_t* cr) {
    if (!visible_) return;

    const float pad = 16.0f;
    const float r = 8.0f;
    const float maxW = 600.0f;
    const float w = std::min(sw_ - pad * 2, maxW);
    const float h = 48.0f;
    const float x = sx_ + (sw_ - w) * 0.5f;
    const float y = sy_ + sh_ - h - pad;

    cairo_save(cr);

    // Container
    cairo_set_source_rgba(cr, 0.18f, 0.16f, 0.24f, 1.0f);
    cairo_new_path(cr);
    cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
    cairo_arc(cr, x + w - r, y + r, r, 1.5 * M_PI, 2.0 * M_PI);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, 0.5 * M_PI);
    cairo_arc(cr, x + r, y + h - r, r, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);
    cairo_fill(cr);

    // Text
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.93f);
    cairo_move_to(cr, x + 16, y + h * 0.5f + 5);
    cairo_show_text(cr, text_.c_str());

    // Action label
    if (!action_.empty()) {
      cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 1.0f);
      cairo_set_font_size(cr, 14);
      cairo_text_extents_t te;
      cairo_text_extents(cr, action_.c_str(), &te);
      cairo_move_to(cr, x + w - te.width - 16, y + h * 0.5f + 5);
      cairo_show_text(cr, action_.c_str());
    }

    cairo_restore(cr);
  }

private:
  std::string text_;
  std::string action_;
  bool visible_ = false;
  float sx_ = 0, sy_ = 0, sw_ = 0, sh_ = 0;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
  std::function<void()> onAction_;
};

} // namespace m3
