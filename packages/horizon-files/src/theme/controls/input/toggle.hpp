#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

#include <cairo/cairo.h>

#include "theme/core/animation.hpp"
#include "theme/core/focus_ring.hpp"
#include "theme/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Switch (Toggle) — 3 sizes, state layers, disabled token opacities,
// track outline when off, selected thumb icon.
class Toggle {
public:
  enum class Size { S, M, L };

  Toggle() = default;

  void setOn(bool on, uint64_t nowMs = 0) {
    if (on == on_) return;
    on_ = on;
    thumbTween_.start(thumbTween_.value(nowMs), on ? 1.0f : 0.0f, nowMs, 200, kEmphasized);
    trackTween_.start(trackTween_.value(nowMs), on ? 1.0f : 0.0f, nowMs, 200, kEmphasized);
  }

  void setSize(Size s) { size_ = s; }
  void setEnabled(bool e) { enabled_ = e; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }
  void setHovered(bool h) { hovered_ = h; }
  void setPressed(bool p) { pressed_ = p; }
  void setFocused(bool f) { focused_ = f; }
  [[nodiscard]] bool pressed() const noexcept { return pressed_; }

  void setOnClick(std::function<void()> cb) { onClick_ = std::move(cb); }
  void setOnToggle(std::function<void(bool)> cb) { onToggle_ = std::move(cb); }

  [[nodiscard]] bool on() const noexcept { return on_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] bool animating() const noexcept { return thumbTween_.active; }

  bool containsPoint(float px, float py) const noexcept {
    return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
  }

  // Event handlers
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
      setOn(!on_, 0);
      if (onToggle_) onToggle_(on_);
      if (onClick_) onClick_();
    }
    return true;
  }

  void paint(cairo_t* cr, uint64_t nowMs = 0) {
    if (w_ <= 0 || h_ <= 0) return;

    const float thumbPos = thumbTween_.value(nowMs);
    const float trackT = trackTween_.value(nowMs);

    // Size constants
    float trackW = 32, trackH = 20, thumbD = 16;
    switch (size_) {
      case Size::S: trackW = 28; trackH = 16; thumbD = 12; break;
      case Size::M: trackW = 32; trackH = 20; thumbD = 16; break;
      case Size::L: trackW = 40; trackH = 24; thumbD = 20; break;
    }

    const float cx = x_ + w_ * 0.5f;
    const float cy = y_ + h_ * 0.5f;
    const float tlx = cx - trackW * 0.5f;
    const float tly = cy - trackH * 0.5f;
    const float trackR = trackH * 0.5f;

    const float handleR = thumbD * 0.5f;
    const float handleMinX = tlx + handleR;
    const float handleMaxX = tlx + trackW - handleR;
    const float hx = handleMinX + (handleMaxX - handleMinX) * thumbPos;
    const float hy = cy;

    // Colours
    const bool isOn = trackT > 0.5f;
    float trackRCol, trackGCol, trackBCol, trackACol;
    float handleRCol, handleGCol, handleBCol, handleACol;

    if (!enabled_) {
      trackRCol = textR_; trackGCol = textR_; trackBCol = textR_;
      trackACol = 0.12f;
      handleRCol = textR_; handleGCol = textR_; handleBCol = textR_;
      handleACol = 0.38f;
    } else {
      const float t = trackT;
      trackRCol = (1 - t) * surfaceR_ + t * accentR_;
      trackGCol = (1 - t) * surfaceG_ + t * accentG_;
      trackBCol = (1 - t) * surfaceB_ + t * accentB_;
      trackACol = isOn ? 1.0f : 0.5f;
      handleRCol = isOn ? 1.0f : outlineR_;
      handleGCol = isOn ? 1.0f : outlineG_;
      handleBCol = isOn ? 1.0f : outlineB_;
      handleACol = 1.0f;
    }

    cairo_save(cr);

    // Track
    cairo_new_path(cr);
    cairo_arc(cr, tlx + trackR, tly + trackR, trackR, M_PI, 1.5 * M_PI);
    cairo_arc(cr, tlx + trackW - trackR, tly + trackR, trackR, 1.5 * M_PI, 2.0 * M_PI);
    cairo_arc(cr, tlx + trackW - trackR, tly + trackH - trackR, trackR, 0.0, 0.5 * M_PI);
    cairo_arc(cr, tlx + trackR, tly + trackH - trackR, trackR, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, trackRCol, trackGCol, trackBCol, trackACol);
    cairo_fill(cr);

    // Track outline (when off)
    if (!isOn && enabled_) {
      cairo_new_path(cr);
      cairo_arc(cr, tlx + trackR, tly + trackR, trackR - 1.0f, M_PI, 1.5 * M_PI);
      cairo_arc(cr, tlx + trackW - trackR, tly + trackR, trackR - 1.0f, 1.5 * M_PI, 2.0 * M_PI);
      cairo_arc(cr, tlx + trackW - trackR, tly + trackH - trackR, trackR - 1.0f, 0.0, 0.5 * M_PI);
      cairo_arc(cr, tlx + trackR, tly + trackH - trackR, trackR - 1.0f, 0.5 * M_PI, M_PI);
      cairo_close_path(cr);
      cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 1.0f);
      cairo_set_line_width(cr, 2.0f);
      cairo_stroke(cr);
    }

    // Handle (thumb)
    cairo_set_source_rgba(cr, handleRCol, handleGCol, handleBCol, handleACol);
    cairo_arc(cr, hx, hy, handleR, 0, 2.0 * M_PI);
    cairo_fill(cr);

    cairo_restore(cr);

    // State layer
    stateLayer_.setColor(isOn ? accentR_ : outlineR_, isOn ? accentG_ : outlineG_, isOn ? accentB_ : outlineB_);
    stateLayer_.setRadius(trackR);
    stateLayer_.setGeometry(hx - handleR * 3, hy - handleR * 3, handleR * 6, handleR * 6);
    stateLayer_.setHovered(hovered_);
    stateLayer_.setPressed(pressed_);
    stateLayer_.setFocused(focused_);
    stateLayer_.tick(nowMs);
    stateLayer_.paint(cr, nowMs);

    // Focus ring
    focusRing_.setFocused(focused_);
    focusRing_.setColor(outlineR_, outlineG_, outlineB_);
    focusRing_.setRadius(handleR + 4);
    focusRing_.paint(cr, hx - handleR - 4, hy - handleR - 4, (handleR + 4) * 2, (handleR + 4) * 2);
  }

  void paint(cairo_t* cr) const {
    const_cast<Toggle*>(this)->paint(cr, 0);
  }

private:
  Size size_ = Size::M;
  bool on_ = false;
  bool enabled_ = true;
  bool hovered_ = false, pressed_ = false, focused_ = false;

  float x_ = 0, y_ = 0, w_ = 52, h_ = 26;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;

  Tween thumbTween_;
  Tween trackTween_;
  StateLayer stateLayer_;
  FocusRing focusRing_;

  std::function<void()> onClick_;
  std::function<void(bool)> onToggle_;
};

} // namespace m3
