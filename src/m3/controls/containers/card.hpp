#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

#include <cairo/cairo.h>

#include "m3/core/primitives/box.hpp"
#include "m3/core/focus_ring.hpp"
#include "m3/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Card — 3 variants: Elevated, Filled, Outlined.
// Content container with state layer, surface tint, and optional shadow/outline.
class Card {
public:
  enum class Variant { Elevated, Filled, Outlined };

  Card() = default;

  void setVariant(Variant v) { variant_ = v; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }
  void setHovered(bool h) { hovered_ = h; }
  void setPressed(bool p) { pressed_ = p; }
  void setFocused(bool f) { focused_ = f; }
  void setElevated(bool e) { elevated_ = e; }

  void setOnClick(std::function<void()> cb) { onClick_ = std::move(cb); }

  [[nodiscard]] Variant variant() const noexcept { return variant_; }
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

    const float r = 12.0f;  // corner.medium

    // Container colours per variant
    float bgR, bgG, bgB, bgA;
    bool hasOutline = false;
    float oR = 0, oG = 0, oB = 0;

    switch (variant_) {
      case Variant::Elevated:
        bgR = blend(surfaceR_, outlineR_, 0.04f);
        bgG = blend(surfaceG_, outlineG_, 0.04f);
        bgB = blend(surfaceB_, outlineB_, 0.04f);
        bgA = 1.0f;
        break;
      case Variant::Filled:
        bgR = blend(surfaceR_, outlineR_, 0.08f);
        bgG = blend(surfaceG_, outlineG_, 0.08f);
        bgB = blend(surfaceB_, outlineB_, 0.08f);
        bgA = 1.0f;
        break;
      case Variant::Outlined:
        bgR = surfaceR_; bgG = surfaceG_; bgB = surfaceB_; bgA = 1.0f;
        hasOutline = true;
        oR = outlineR_; oG = outlineG_; oB = outlineB_;
        break;
    }

    cairo_save(cr);

    // Shadow (Elevated variant only)
    if (variant_ == Variant::Elevated) {
      const float shadowA = elevated_ ? 0.24f : 0.16f;
      cairo_new_path(cr);
      cairo_arc(cr, x_ + r, y_ + r, r, M_PI, 1.5 * M_PI);
      cairo_arc(cr, x_ + w_ - r, y_ + r, r, 1.5 * M_PI, 2.0 * M_PI);
      cairo_arc(cr, x_ + w_ - r, y_ + h_ - r, r, 0.0, 0.5 * M_PI);
      cairo_arc(cr, x_ + r, y_ + h_ - r, r, 0.5 * M_PI, M_PI);
      cairo_close_path(cr);
      cairo_set_source_rgba(cr, 0, 0, 0, shadowA);
      cairo_set_line_width(cr, elevated_ ? 3.0f : 1.0f);
      cairo_stroke(cr);
    }

    // Background fill (Box provides the glassy inner rim/highlights, radius-conformant)
    Box bgBox;
    bgBox.setColor(bgR, bgG, bgB, bgA);
    bgBox.setRadius(r);
    bgBox.setGeometry(x_, y_, w_, h_);
    bgBox.setGlassy(true);
    bgBox.paint(cr);

    // Outline (Outlined variant)
    if (hasOutline) {
      cairo_new_path(cr);
      cairo_arc(cr, x_ + r, y_ + r, r - 0.5f, M_PI, 1.5 * M_PI);
      cairo_arc(cr, x_ + w_ - r, y_ + r, r - 0.5f, 1.5 * M_PI, 2.0 * M_PI);
      cairo_arc(cr, x_ + w_ - r, y_ + h_ - r, r - 0.5f, 0.0, 0.5 * M_PI);
      cairo_arc(cr, x_ + r, y_ + h_ - r, r - 0.5f, 0.5 * M_PI, M_PI);
      cairo_close_path(cr);
      cairo_set_source_rgba(cr, oR, oG, oB, 0.5f);
      cairo_set_line_width(cr, 1.0f);
      cairo_stroke(cr);
    }

    // Surface tint layer (Elevated variant)
    if (variant_ == Variant::Elevated) {
      cairo_new_path(cr);
      cairo_arc(cr, x_ + r, y_ + r, r, M_PI, 1.5 * M_PI);
      cairo_arc(cr, x_ + w_ - r, y_ + r, r, 1.5 * M_PI, 2.0 * M_PI);
      cairo_arc(cr, x_ + w_ - r, y_ + h_ - r, r, 0.0, 0.5 * M_PI);
      cairo_arc(cr, x_ + r, y_ + h_ - r, r, 0.5 * M_PI, M_PI);
      cairo_close_path(cr);
      cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 0.05f);
      cairo_fill(cr);
    }

    cairo_restore(cr);

    // State layer
    stateLayer_.setColor(textR_, textG_, textB_);
    stateLayer_.setRadius(r);
    stateLayer_.setGeometry(x_, y_, w_, h_);
    stateLayer_.setHovered(hovered_);
    stateLayer_.setPressed(pressed_);
    stateLayer_.setFocused(focused_);
    stateLayer_.tick(0);
    stateLayer_.paint(cr, 0);

    // Focus ring
    focusRing_.setFocused(focused_);
    focusRing_.setColor(outlineR_, outlineG_, outlineB_);
    focusRing_.setRadius(r);
    focusRing_.paint(cr, x_, y_, w_, h_);
  }

private:
  static float blend(float a, float b, float t) {
    return a + (b - a) * t;
  }

  Variant variant_ = Variant::Filled;
  bool hovered_ = false, pressed_ = false, focused_ = false;
  bool elevated_ = false;

  float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;

  StateLayer stateLayer_;
  FocusRing focusRing_;
  std::function<void()> onClick_;
};

} // namespace m3
