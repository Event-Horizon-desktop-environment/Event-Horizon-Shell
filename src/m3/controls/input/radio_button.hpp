#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

#include <cairo/cairo.h>

#include "m3/core/animation.hpp"
#include "m3/core/focus_ring.hpp"
#include "m3/core/primitives/state_layer.hpp"

namespace m3 {

// M3 RadioButton — selected/unselected, state layer, focus ring, animated dot.
class RadioButton {
public:
  RadioButton() = default;

  void setSelected(bool s, uint64_t nowMs = 0) {
    if (s == selected_) return;
    selected_ = s;
    animTween_.start(animTween_.value(nowMs), s ? 1.0f : 0.0f, nowMs, 200, kEmphasized);
  }

  void setEnabled(bool e) { enabled_ = e; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setHovered(bool h) { hovered_ = h; }
  void setPressed(bool p) { pressed_ = p; }
  void setFocused(bool f) { focused_ = f; }

  void setOnClick(std::function<void()> cb) { onClick_ = std::move(cb); }
  void setOnSelect(std::function<void()> cb) { onSelect_ = std::move(cb); }

  [[nodiscard]] bool selected() const noexcept { return selected_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }

  bool containsPoint(float px, float py) const noexcept {
    const float cx = x_ + w_ * 0.5f;
    const float cy = y_ + h_ * 0.5f;
    const float r = 24.0f; // 48dp touch target
    return (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r;
  }

  bool handlePointerEnter(float, float) {
    if (!enabled_) return false;
    hovered_ = true;
    return true;
  }

  bool handlePointerLeave() {
    hovered_ = false;
    pressed_ = false;
    return true;
  }

  bool handlePointerDown(float px, float py) {
    if (!enabled_ || !containsPoint(px, py)) return false;
    pressed_ = true;
    return true;
  }

  bool handlePointerUp(float, float) {
    if (!enabled_) return false;
    if (pressed_) {
      pressed_ = false;
      if (!selected_) {
        selected_ = true;
        if (onSelect_) onSelect_();
      }
      if (onClick_) onClick_();
    }
    return true;
  }

  void paint(cairo_t* cr) { paint(cr, 0); }

  void paint(cairo_t* cr, uint64_t nowMs) {
    if (w_ <= 0 || h_ <= 0) return;

    const float cx = x_ + w_ * 0.5f;
    const float cy = y_ + h_ * 0.5f;
    const float outerR = 10.0f;  // 20dp visual
    const float innerR = 5.0f;

    const float animT = animTween_.value(nowMs);
    const float alpha = enabled_ ? 1.0f : 0.38f;

    // Colours
    float ringR, ringG, ringB;

    if (!enabled_) {
      ringR = textR_; ringG = textR_; ringB = textR_;
    } else if (selected_) {
      ringR = accentR_; ringG = accentG_; ringB = accentB_;
    } else {
      ringR = textR_; ringG = textG_; ringB = textB_;
    }

    cairo_save(cr);

    // Outer ring
    cairo_set_source_rgba(cr, ringR, ringG, ringB, alpha);
    cairo_set_line_width(cr, 2.0f);
    cairo_arc(cr, cx, cy, outerR, 0, 2.0 * M_PI);
    cairo_stroke(cr);

    // Inner dot (grows on select)
    if (animT > 0.0f) {
      const float dotR = innerR * animT;
      cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha * animT);
      cairo_arc(cr, cx, cy, dotR, 0, 2.0 * M_PI);
      cairo_fill(cr);
    }

    cairo_restore(cr);

    // State layer
    stateLayer_.setColor(selected_ ? accentR_ : textR_,
                          selected_ ? accentG_ : textG_,
                          selected_ ? accentB_ : textB_);
    stateLayer_.setRadius(outerR + 8);
    stateLayer_.setGeometry(cx - outerR - 8, cy - outerR - 8, (outerR + 8) * 2, (outerR + 8) * 2);
    stateLayer_.setHovered(hovered_);
    stateLayer_.setPressed(pressed_);
    stateLayer_.setFocused(focused_);
    stateLayer_.tick(nowMs);
    stateLayer_.paint(cr, nowMs);

    // Focus ring
    focusRing_.setFocused(focused_);
    focusRing_.setColor(textR_, textG_, textB_);
    focusRing_.setRadius(outerR + 6);
    focusRing_.paint(cr, cx - outerR - 6, cy - outerR - 6, (outerR + 6) * 2, (outerR + 6) * 2);
  }

private:
  bool selected_ = false;
  bool enabled_ = true;
  bool hovered_ = false, pressed_ = false, focused_ = false;

  float x_ = 0, y_ = 0, w_ = 48, h_ = 48;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;

  Tween animTween_;
  StateLayer stateLayer_;
  FocusRing focusRing_;

  std::function<void()> onClick_;
  std::function<void()> onSelect_;
};

} // namespace m3
