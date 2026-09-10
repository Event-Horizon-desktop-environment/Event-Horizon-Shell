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

// M3 Bottom Sheet — Modal or Standard, slides up from bottom.
class BottomSheet {
public:
  enum class Variant { Modal, Standard };

  BottomSheet() = default;

  void setVariant(Variant v) { variant_ = v; }
  void setVisible(bool v) { visible_ = v; }
  void setGeometry(float x, float y, float w, float h) { sx_ = x; sy_ = y; sw_ = w; sh_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }

  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] bool modal() const noexcept { return variant_ == Variant::Modal; }

  bool hitScrim(float px, float py) const {
    if (!visible_ || !modal_) return false;
    return px >= sx_ && px < sx_ + sw_ && py >= sy_ && py < sheetTop();
  }

  void paint(cairo_t* cr) {
    if (!visible_ || sw_ <= 0) return;

    const float sheetH = std::min(sh_ * 0.6f, 400.0f);
    const float sheetY = sy_ + sh_ - sheetH;
    const float r = 16.0f;

    // Scrim (modal only)
    if (modal_) {
      cairo_save(cr);
      cairo_set_source_rgba(cr, 0.071f, 0.047f, 0.149f, 0.32f);
      cairo_rectangle(cr, sx_, sy_, sw_, sh_);
      cairo_fill(cr);
      cairo_restore(cr);
    }

    // Sheet surface
    cairo_save(cr);
    cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 1.0f);
    cairo_new_path(cr);
    cairo_arc(cr, sx_ + r, sheetY + r, r, M_PI, 1.5 * M_PI);
    cairo_arc(cr, sx_ + sw_ - r, sheetY + r, r, 1.5 * M_PI, 2.0 * M_PI);
    cairo_line_to(cr, sx_ + sw_, sy_ + sh_);
    cairo_line_to(cr, sx_, sy_ + sh_);
    cairo_close_path(cr);
    cairo_fill(cr);

    // Drag handle
    const float hx = sx_ + sw_ * 0.5f;
    cairo_set_source_rgba(cr, textR_, textG_, textB_, 0.3f);
    cairo_set_line_width(cr, 4);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, hx - 16, sheetY + 8);
    cairo_line_to(cr, hx + 16, sheetY + 8);
    cairo_stroke(cr);

    cairo_restore(cr);
  }

private:
  float sheetTop() const {
    const float sheetH = std::min(sh_ * 0.6f, 400.0f);
    return sy_ + sh_ - sheetH;
  }

  Variant variant_ = Variant::Modal;
  bool visible_ = false;
  float sx_ = 0, sy_ = 0, sw_ = 0, sh_ = 0;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
};

} // namespace m3
