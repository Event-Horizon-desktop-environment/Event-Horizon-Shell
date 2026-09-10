#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

#include <cairo/cairo.h>

#include "theme/core/animation.hpp"
#include "theme/core/focus_ring.hpp"
#include "theme/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Slider — continuous/discrete, active+inactive track, stop indicator,
// value indicator popup, thumb press shrink, centered variant.
class Slider {
public:
  enum class Size { XS, S, M, L, XL };

  Slider() = default;

  void setRange(float lo, float hi) {
    if (hi < lo) std::swap(lo, hi);
    lo_ = lo; hi_ = hi;
    applyValue(value_);
  }

  void setStep(float step) { step_ = std::max(step, 0.0f); applyValue(value_); }
  void setValue(float val) { animDurationMs_ = 0; applyValue(val); }

  void animateToValue(float val, uint64_t nowMs) {
    if (dragging_ || std::abs(val - value_) < 0.0001f) { applyValue(val); return; }
    animFrom_ = value_;
    animTo_ = snap(val);
    animStartMs_ = nowMs;
    animDurationMs_ = 100;
  }

  void setSize(Size s) { size_ = s; }
  void setEnabled(bool e) { enabled_ = e; if (!e) { hovered_ = false; dragging_ = false; } }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setHovered(bool h) { forceHover_ = true; hovered_ = h; }
  void setPressed(bool p) { forcePressed_ = true; dragging_ = p; }
  void setFocused(bool f) { focused_ = f; }
  void setCentered(bool c) { centered_ = c; }
  void setShowValueLabel(bool s) { showValueLabel_ = s; }
  void setValueLabel(const char* text) { valueLabel_ = text ? text : ""; }

  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }

  void setOnValueChanged(std::function<void(float)> cb) { onValueChanged_ = std::move(cb); }
  void setOnDragStarted(std::function<void()> cb) { onDragStarted_ = std::move(cb); }
  void setOnDragFinished(std::function<void()> cb) { onDragFinished_ = std::move(cb); }

  [[nodiscard]] float value() const noexcept { return value_; }
  [[nodiscard]] float minValue() const noexcept { return lo_; }
  [[nodiscard]] float maxValue() const noexcept { return hi_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] bool dragging() const noexcept { return dragging_; }
  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }
  [[nodiscard]] bool isAnimating() const noexcept { return animDurationMs_ > 0; }
  [[nodiscard]] float animatedValue(uint64_t nowMs) const;

  bool containsPoint(float px, float py) const noexcept;
  float grabOffset(float px) const noexcept;

  bool handlePointerEnter(float, float);
  bool handlePointerLeave();
  bool handlePointerDown(float px, float py);
  bool handlePointerMove(float px, float);
  bool handlePointerUp(float, float);
  bool handleScroll(float delta);

  void paint(cairo_t* cr) const;
  void paint(cairo_t* cr, uint64_t nowMs);

private:
  void applyValue(float raw);
  [[nodiscard]] float snap(float val) const noexcept;
  [[nodiscard]] float normalized() const noexcept;
  [[nodiscard]] float normalizedCentered() const noexcept;
  [[nodiscard]] float thumbCenter() const noexcept;
  [[nodiscard]] float trackLeft() const noexcept;
  [[nodiscard]] float trackWidth() const noexcept;

  void drawStopIndicator(cairo_t* cr, float tl, float tw, float ty) const;
  void drawValuePopup(cairo_t* cr, float tc, float ty, float tw, float alpha) const;

  // Size constants
  static constexpr float kThumbSize[] = { 16.0f, 16.0f, 20.0f, 24.0f, 24.0f };
  static constexpr float kTrackHeight[] = { 4.0f, 4.0f, 4.0f, 6.0f, 6.0f };

  float lo_ = 0.0f, hi_ = 100.0f, step_ = 1.0f, value_ = 50.0f;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
  Size size_ = Size::M;
  bool enabled_ = true, hovered_ = false, dragging_ = false;
  bool forceHover_ = false, forcePressed_ = false;
  bool centered_ = false;
  bool showValueLabel_ = false;
  bool focused_ = false;
  std::string valueLabel_;
  float grabOffsetX_ = 0;

  uint64_t animStartMs_ = 0;
  float animFrom_ = 0, animTo_ = 0;
  uint64_t animDurationMs_ = 0;

  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;

  StateLayer stateLayer_;
  FocusRing focusRing_;

  std::function<void(float)> onValueChanged_;
  std::function<void()> onDragStarted_;
  std::function<void()> onDragFinished_;
};

// --- inline implementations ---

inline float Slider::animatedValue(uint64_t nowMs) const {
  if (animDurationMs_ == 0) return value_;
  const uint64_t elapsed = nowMs - animStartMs_;
  if (elapsed >= animDurationMs_) return animTo_;
  const float t = static_cast<float>(elapsed) / static_cast<float>(animDurationMs_);
  const float ease = 1.0f - std::pow(1.0f - t, 3.0f);
  return animFrom_ + (animTo_ - animFrom_) * ease;
}

inline float Slider::normalized() const noexcept {
  if (hi_ <= lo_) return 0.0f;
  return std::clamp((value_ - lo_) / (hi_ - lo_), 0.0f, 1.0f);
}

inline float Slider::normalizedCentered() const noexcept {
  if (hi_ <= lo_) return 0.0f;
  const float n = (value_ - lo_) / (hi_ - lo_);
  return n * 2.0f - 1.0f; // -1 to +1
}

inline float Slider::trackLeft() const noexcept {
  const float ts = kThumbSize[static_cast<int>(size_)] * 0.5f;
  return x_ + ts;
}

inline float Slider::trackWidth() const noexcept {
  const float ts = kThumbSize[static_cast<int>(size_)];
  return std::max(0.0f, w_ - ts);
}

inline float Slider::thumbCenter() const noexcept {
  if (centered_) {
    return trackLeft() + (normalizedCentered() * 0.5f + 0.5f) * trackWidth();
  }
  return trackLeft() + normalized() * trackWidth();
}

inline float Slider::snap(float val) const noexcept {
  const float clamped = std::clamp(val, lo_, hi_);
  if (step_ <= 0.0f || hi_ <= lo_) return clamped;
  const float steps = std::round((clamped - lo_) / step_);
  return std::clamp(lo_ + steps * step_, lo_, hi_);
}

inline void Slider::applyValue(float raw) {
  const float snapped = snap(raw);
  if (std::abs(snapped - value_) < 0.0001f) return;
  value_ = snapped;
  if (onValueChanged_) onValueChanged_(value_);
}

inline bool Slider::containsPoint(float px, float py) const noexcept {
  return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
}

inline float Slider::grabOffset(float px) const noexcept {
  return px - thumbCenter();
}

inline bool Slider::handlePointerEnter(float, float) {
  if (!enabled_) return false;
  hovered_ = true;
  return true;
}

inline bool Slider::handlePointerLeave() {
  hovered_ = false;
  return true;
}

inline bool Slider::handlePointerDown(float px, float py) {
  if (!enabled_ || !containsPoint(px, py)) return false;
  animDurationMs_ = 0;
  dragging_ = true;
  grabOffsetX_ = grabOffset(px);
  applyValue(lo_ + ((px - grabOffsetX_ - trackLeft()) / trackWidth()) * (hi_ - lo_));
  if (onDragStarted_) onDragStarted_();
  return true;
}

inline bool Slider::handlePointerMove(float px, float) {
  if (!enabled_ || !dragging_) return false;
  applyValue(lo_ + ((px - grabOffsetX_ - trackLeft()) / trackWidth()) * (hi_ - lo_));
  return true;
}

inline bool Slider::handlePointerUp(float, float) {
  if (!enabled_ || !dragging_) return false;
  dragging_ = false;
  if (onDragFinished_) onDragFinished_();
  return true;
}

inline bool Slider::handleScroll(float delta) {
  if (!enabled_) return false;
  const float adj = step_ > 0.0f ? step_ : (hi_ - lo_) * 0.05f;
  applyValue(value_ - delta * adj);
  return true;
}

inline void Slider::drawStopIndicator(cairo_t* cr, float tl, float tw, float ty) const {
  // Small dot at the end of the active track
  const float dotR = 2.0f;
  cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 0.6f);
  cairo_arc(cr, tl + tw, ty, dotR, 0, 2.0 * M_PI);
  cairo_fill(cr);
}

inline void Slider::drawValuePopup(cairo_t* cr, float tc, float ty, float tw, float alpha) const {
  (void)tw;
  if (!showValueLabel_ || valueLabel_.empty()) return;

  const float popupW = 48.0f;
  const float popupH = 24.0f;
  const float popupR = 4.0f;
  const float popupX = tc - popupW * 0.5f;
  const float popupY = ty - popupH - 8.0f - kThumbSize[static_cast<int>(size_)] * 0.5f;

  cairo_save(cr);

  // Popup background
  cairo_new_path(cr);
  cairo_arc(cr, popupX + popupR, popupY + popupR, popupR, M_PI, 1.5 * M_PI);
  cairo_arc(cr, popupX + popupW - popupR, popupY + popupR, popupR, 1.5 * M_PI, 2.0 * M_PI);
  cairo_arc(cr, popupX + popupW - popupR, popupY + popupH - popupR, popupR, 0.0, 0.5 * M_PI);
  cairo_arc(cr, popupX + popupR, popupY + popupH - popupR, popupR, 0.5 * M_PI, M_PI);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha * 0.9f);
  cairo_fill(cr);

  // Arrow from popup to thumb
  cairo_move_to(cr, tc - 4, popupY + popupH);
  cairo_line_to(cr, tc, popupY + popupH + 6);
  cairo_line_to(cr, tc + 4, popupY + popupH);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha * 0.9f);
  cairo_fill(cr);

  // Label text
  cairo_set_source_rgba(cr, 1.0f, 1.0f, 1.0f, alpha);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 10);
  cairo_text_extents_t te;
  cairo_text_extents(cr, valueLabel_.c_str(), &te);
  cairo_move_to(cr, popupX + (popupW - te.x_advance) * 0.5f, popupY + popupH * 0.5f + 4);
  cairo_show_text(cr, valueLabel_.c_str());

  cairo_restore(cr);
}

inline void Slider::paint(cairo_t* cr) const {
  const_cast<Slider*>(this)->paint(cr, 0);
}

inline void Slider::paint(cairo_t* cr, uint64_t nowMs) {
  if (w_ <= 0 || h_ <= 0) return;

  const bool isHov = forceHover_ ? hovered_ : (hovered_ || dragging_);
  const bool isDrag = forcePressed_ ? dragging_ : dragging_;
  const float pv = animatedValue(nowMs);

  const int si = static_cast<int>(size_);
  const float thumbSize = kThumbSize[si];
  const float trackH = kTrackHeight[si];
  const float tl = trackLeft();
  const float tw = trackWidth();
  const float ty = y_ + h_ * 0.5f;
  const float alpha = enabled_ ? 1.0f : 0.38f;

  float tc;
  if (centered_) {
    const float n = (pv - lo_) / (hi_ - lo_);
    tc = tl + (n * 2.0f - 1.0f) * tw * 0.5f + tw * 0.5f;
  } else {
    tc = tl + ((pv - lo_) / (hi_ - lo_)) * tw;
  }

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  // Inactive track
  const float inactAlpha = 0.35f;
  cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, alpha * inactAlpha);
  cairo_set_line_width(cr, trackH);
  cairo_move_to(cr, tl, ty);
  cairo_line_to(cr, tl + tw, ty);
  cairo_stroke(cr);

  // Active track
  cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha);
  cairo_set_line_width(cr, trackH);
  cairo_move_to(cr, tl, ty);
  cairo_line_to(cr, tc, ty);
  cairo_stroke(cr);

  // Stop indicator at end of track
  if (enabled_) drawStopIndicator(cr, tl, tw, ty);

  // Thumb
  const float ts = isDrag ? thumbSize * 1.1f : thumbSize;
  const float thumbR = ts * 0.5f;

  // State layer on thumb
  stateLayer_.setColor(accentR_, accentG_, accentB_);
  stateLayer_.setRadius(thumbR + 8.0f);
  stateLayer_.setGeometry(tc - thumbR - 8.0f, ty - thumbR - 8.0f, ts + 16.0f, ts + 16.0f);
  stateLayer_.setHovered(isHov);
  stateLayer_.setPressed(isDrag);
  stateLayer_.tick(nowMs);
  stateLayer_.paint(cr, nowMs);

  // Thumb circle
  cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha);
  cairo_arc(cr, tc, ty, thumbR, 0, 2.0 * M_PI);
  cairo_fill(cr);

  // Value popup
  if (showValueLabel_ && isDrag) drawValuePopup(cr, tc, ty, tw, alpha);

  // Focus ring
  focusRing_.setFocused(focused_);
  focusRing_.setColor(accentR_, accentG_, accentB_);
  focusRing_.setRadius(thumbR + 6);
  focusRing_.paint(cr, tc - thumbR - 6, ty - thumbR - 6, (thumbR + 6) * 2, (thumbR + 6) * 2);
}

} // namespace m3
