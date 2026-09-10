#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <cairo/cairo.h>

#include "theme/core/focus_ring.hpp"
#include "theme/core/primitives/box.hpp"
#include "theme/core/primitives/state_layer.hpp"

namespace m3 {

// M3 FAB — Standard, Small, Large, Extended.
class FAB {
public:
  enum class Size { Standard, Small, Large, Extended };

  FAB() = default;

  void setSize(Size s) { size_ = s; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }
  void setIcon(std::string_view ic) { icon_ = ic; }
  void setLabel(std::string_view l) { label_ = l; }
  void setHovered(bool h) { hovered_ = h; }
  void setPressed(bool p) { pressed_ = p; }
  void setFocused(bool f) { focused_ = f; }
  void setLowered(bool l) { lowered_ = l; }
  void setOnClick(std::function<void()> cb) { onClick_ = std::move(cb); }

  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }

  bool containsPoint(float px, float py) const noexcept {
    return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
  }

  bool handlePointerEnter(float, float) { hovered_ = true; return true; }
  bool handlePointerLeave() { hovered_ = false; pressed_ = false; return true; }

  bool handlePointerDown(float px, float py) {
    if (!containsPoint(px, py)) return false;
    pressed_ = true;
    return true;
  }

  bool handlePointerUp(float, float) {
    if (pressed_) { pressed_ = false; if (onClick_) onClick_(); }
    return true;
  }

  void paint(cairo_t* cr) {
    if (w_ <= 0 || h_ <= 0) return;

    // Size constants
    float d = 56.0f;
    float iconS = 24.0f;
    float fontS = 14.0f;
    float elevation = 6.0f;
    switch (size_) {
      case Size::Small:    d = 40.0f; iconS = 24.0f; fontS = 14.0f; elevation = 4.0f; break;
      case Size::Standard: d = 56.0f; iconS = 24.0f; fontS = 14.0f; elevation = 6.0f; break;
      case Size::Large:    d = 96.0f; iconS = 36.0f; fontS = 14.0f; elevation = 6.0f; break;
      case Size::Extended: d = 56.0f; iconS = 24.0f; fontS = 14.0f; elevation = 6.0f; break;
    }

    const bool ext = size_ == Size::Extended;
    const float r = d * 0.5f;
    const float cx = x_ + w_ * 0.5f;
    const float cy = y_ + h_ * 0.5f;

    cairo_save(cr);

    // Shadow
    if (!lowered_) {
      cairo_set_source_rgba(cr, 0, 0, 0, 0.12f);
      cairo_arc(cr, cx + 1, cy + elevation * 0.5f, r, 0, 2.0 * M_PI);
      cairo_fill(cr);
    }

    // Container (Box for glassy, using exact circular radius for conformance)
    {
      Box bgBox;
      if (lowered_) {
        bgBox.setColor(surfaceR_, surfaceG_, surfaceB_, 1.0f);
      } else {
        bgBox.setColor(accentR_, accentG_, accentB_, 1.0f);
      }
      bgBox.setRadius(r);
      bgBox.setGeometry(cx - r, cy - r, d, d);
      bgBox.setGlassy(true);
      bgBox.paint(cr);
    }

    cairo_restore(cr);

    // Icon
    if (!icon_.empty()) {
      cairo_save(cr);
      cairo_select_font_face(cr, "Symbols Nerd Font", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, iconS);
      cairo_set_source_rgba(cr, lowered_ ? accentR_ : 1.0f, lowered_ ? accentG_ : 1.0f,
                             lowered_ ? accentB_ : 1.0f, 1.0f);
      const float ix = ext ? cx - d * 0.3f : cx;
      cairo_move_to(cr, ix - iconS * 0.3f, cy + iconS * 0.3f);
      cairo_show_text(cr, icon_.c_str());
      cairo_restore(cr);
    }

    // Label (Extended only)
    if (ext && !label_.empty()) {
      cairo_save(cr);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, fontS);
      cairo_set_source_rgba(cr, lowered_ ? textR_ : 1.0f, lowered_ ? textG_ : 1.0f,
                             lowered_ ? textB_ : 1.0f, 1.0f);
      cairo_text_extents_t te;
      cairo_text_extents(cr, label_.c_str(), &te);
      cairo_move_to(cr, cx + d * 0.15f, cy + te.height * 0.35f);
      cairo_show_text(cr, label_.c_str());
      cairo_restore(cr);
    }

    // State layer
    stateLayer_.setColor(lowered_ ? textR_ : 1.0f, lowered_ ? textG_ : 1.0f,
                          lowered_ ? textB_ : 1.0f);
    stateLayer_.setRadius(r);
    stateLayer_.setGeometry(cx - r, cy - r, d, d);
    stateLayer_.setHovered(hovered_);
    stateLayer_.setPressed(pressed_);
    stateLayer_.setFocused(focused_);
    stateLayer_.tick(0);
    stateLayer_.paint(cr, 0);

    // Focus ring
    focusRing_.setFocused(focused_);
    focusRing_.setColor(outlineR_, outlineG_, outlineB_);
    focusRing_.setRadius(r + 4);
    focusRing_.paint(cr, cx - r - 4, cy - r - 4, d + 8, d + 8);
  }

private:
  Size size_ = Size::Standard;
  bool hovered_ = false, pressed_ = false, focused_ = false, lowered_ = false;
  float x_ = 0, y_ = 0, w_ = 56, h_ = 56;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
  std::string icon_;
  std::string label_;
  StateLayer stateLayer_;
  FocusRing focusRing_;
  std::function<void()> onClick_;
};

} // namespace m3
