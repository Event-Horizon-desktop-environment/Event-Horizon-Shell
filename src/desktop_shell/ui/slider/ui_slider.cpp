#include "desktop_shell/ui/slider/ui_slider.hpp"

#include <algorithm>
#include <cmath>
#include <cairo/cairo.h>

namespace eh::ui {

Slider::Slider() = default;

void Slider::setRange(float lo, float hi) {
   
  if (hi < lo) std::swap(lo, hi);
  if (lo_ == lo && hi_ == hi) return;
  lo_ = lo;
  hi_ = hi;
  applyValue(value_);
}

void Slider::setStep(float step) {
   
  step_ = std::max(step, 0.0f);
  applyValue(value_);
}

void Slider::setValue(float val) {
   
  animDurationMs_ = 0;
  applyValue(val);
}

void Slider::animateToValue(float val, uint64_t nowMs) {
   
  if (dragging_ || std::abs(val - value_) < 0.0001f) {
    applyValue(val);
    return;
  }
  animFrom_ = value_;
  animTo_ = snap(val);
  animStartMs_ = nowMs;
  animDurationMs_ = 60;
}

float Slider::animatedValue(uint64_t nowMs) const {
   
  if (animDurationMs_ == 0) return value_;
  const uint64_t elapsed = nowMs - animStartMs_;
  if (elapsed >= animDurationMs_) return animTo_;
  const float t = static_cast<float>(elapsed) / static_cast<float>(animDurationMs_);
  const float ease = 1.0f - std::pow(1.0f - t, 3.0f);
  return animFrom_ + (animTo_ - animFrom_) * ease;
}

void Slider::setEnabled(bool enabled) {
   
  enabled_ = enabled;
  if (!enabled_) {
    hovered_ = false;
    dragging_ = false;
  }
}

void Slider::setGeometry(float x, float y, float w, float h) {
   
  x_ = x;
  y_ = y;
  w_ = w;
  h_ = h;
}

void Slider::setAccentColor(float r, float g, float b) {
   
  accentR_ = r; accentG_ = g; accentB_ = b;
}

void Slider::setSurfaceColor(float r, float g, float b) {
   
  surfaceR_ = r; surfaceG_ = g; surfaceB_ = b;
}

void Slider::setTextColor(float r, float g, float b) {
   
  textR_ = r; textG_ = g; textB_ = b;
}

void Slider::setTrackColor(float r, float g, float b) {
   
  trackR_ = r; trackG_ = g; trackB_ = b;
}

void Slider::setHovered(bool h) {
   
  forceHover_ = true;
  hovered_ = h;
}

void Slider::setPressed(bool p) {
   
  forcePressed_ = true;
  dragging_ = p;
}

void Slider::setShowValueLabel(bool show) { showValueLabel_ = show; }

void Slider::setValueLabel(const char* text) { valueLabel_ = text ? text : ""; }

void Slider::setOnValueChanged(std::function<void(float)> cb) {
   
  onValueChanged_ = std::move(cb);
}

void Slider::setOnDragStarted(std::function<void()> cb) {
   
  onDragStarted_ = std::move(cb);
}

void Slider::setOnDragFinished(std::function<void()> cb) {
   
  onDragFinished_ = std::move(cb);
}

float Slider::normalized() const noexcept {
   
  if (hi_ <= lo_) return 0.0f;
  return std::clamp((value_ - lo_) / (hi_ - lo_), 0.0f, 1.0f);
}

float Slider::trackLeft() const noexcept {
   
  return x_ + thumbSize_ * 0.5f;
}

float Slider::trackWidth() const noexcept {
   
  return std::max(0.0f, w_ - thumbSize_);
}

float Slider::thumbCenter() const noexcept {
   
  return trackLeft() + normalized() * trackWidth();
}

float Slider::snap(float val) const noexcept {
   
  const float clamped = std::clamp(val, lo_, hi_);
  if (step_ <= 0.0f || hi_ <= lo_) return clamped;
  const float steps = std::round((clamped - lo_) / step_);
  return std::clamp(lo_ + steps * step_, lo_, hi_);
}

void Slider::applyValue(float raw) {
   
  const float snapped = snap(raw);
  if (std::abs(snapped - value_) < 0.0001f) return;
  value_ = snapped;
  if (onValueChanged_) onValueChanged_(value_);
}

bool Slider::containsPoint(float px, float py) const noexcept {
  return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
}

float Slider::grabOffset(float px) const noexcept {
  return px - thumbCenter();
}

bool Slider::handlePointerEnter(float /*px*/, float /*py*/) {
   
  if (!enabled_) return false;
  hovered_ = true;
  return true;
}

bool Slider::handlePointerLeave() {
   
  hovered_ = false;
  return true;
}

bool Slider::handlePointerDown(float px, float py) {
   
  if (!enabled_ || !containsPoint(px, py)) return false;
  animDurationMs_ = 0;
  dragging_ = true;
  grabOffsetX_ = grabOffset(px);
  applyValue(lo_ + ((px - grabOffsetX_ - trackLeft()) / trackWidth()) * (hi_ - lo_));
  if (onDragStarted_) onDragStarted_();
  return true;
}

bool Slider::handlePointerMove(float px, float /*py*/) {
   
  if (!enabled_ || !dragging_) return false;
  applyValue(lo_ + ((px - grabOffsetX_ - trackLeft()) / trackWidth()) * (hi_ - lo_));
  return true;
}

bool Slider::handlePointerUp(float /*px*/, float /*py*/) {
   
  if (!enabled_ || !dragging_) return false;
  dragging_ = false;
  if (onDragFinished_) onDragFinished_();
  return true;
}

bool Slider::handleScroll(float delta) {
   
  if (!enabled_) return false;
  const float adj = step_ > 0.0f ? step_ : (hi_ - lo_) * 0.05f;
  applyValue(value_ - delta * adj);
  return true;
}

void Slider::drawThumbAt(cairo_t* cr, float tc, float ts, float ty, float alpha, bool isHov, bool isDrag) const {
    
  const float thumbR = ts * 0.5f;

  if (isHov || isDrag) {
    cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, isDrag ? 0.18f : 0.10f);
    cairo_arc(cr, tc, ty, thumbR + 8.0f, 0.0, 2.0 * M_PI);
    cairo_fill(cr);
  }

  cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha);
  cairo_arc(cr, tc, ty, thumbR, 0.0, 2.0 * M_PI);
  cairo_fill(cr);
}

void Slider::paintBody(cairo_t* cr, float paintValue, bool isHov, bool isDrag) const {
   
  if (w_ <= 0.0f || h_ <= 0.0f) return;

  const float tl = trackLeft();
  const float tw = trackWidth();
  const float tc = trackLeft() + ((paintValue - lo_) / (hi_ - lo_)) * trackWidth();
  const float ty = y_ + h_ * 0.5f;
  const float alpha = enabled_ ? 1.0f : 0.4f;

  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  const float trackAlpha = 0.35f;
  cairo_set_source_rgba(cr, trackR_, trackG_, trackB_, alpha * trackAlpha);
  cairo_set_line_width(cr, trackHeight_);
  cairo_move_to(cr, tl, ty);
  cairo_line_to(cr, tl + tw, ty);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha * 0.85f);
  cairo_set_line_width(cr, trackHeight_);
  cairo_move_to(cr, tl, ty);
  cairo_line_to(cr, tc, ty);
  cairo_stroke(cr);

  const float ts = isDrag ? thumbSize_ * 1.25f : thumbSize_;
  drawThumbAt(cr, tc, ts, ty, alpha, isHov, isDrag);

  if (showValueLabel_ && !valueLabel_.empty()) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 10);
    cairo_set_source_rgba(cr, textR_, textG_, textB_, alpha * 0.80f);
    cairo_text_extents_t te;
    cairo_text_extents(cr, valueLabel_.c_str(), &te);
    cairo_move_to(cr, x_ + w_ + 44.0f - static_cast<float>(te.x_advance), ty + 4.0f);
    cairo_show_text(cr, valueLabel_.c_str());
  }
}

void Slider::paint(cairo_t* cr) const {
   
  const bool isHov = forceHover_ ? hovered_ : (hovered_ || dragging_);
  const bool isDrag = forcePressed_ ? dragging_ : dragging_;
  paintBody(cr, value_, isHov, isDrag);
}

void Slider::paint(cairo_t* cr, uint64_t nowMs) const {
   
  const bool isHov = forceHover_ ? hovered_ : (hovered_ || dragging_);
  const bool isDrag = forcePressed_ ? dragging_ : dragging_;
  const float pv = animatedValue(nowMs);
  paintBody(cr, pv, isHov, isDrag);
}

}
