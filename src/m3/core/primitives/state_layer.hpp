#pragma once

#include <cmath>
#include <cstdint>

#include <cairo/cairo.h>

#include "m3/core/animation.hpp"

namespace m3 {

// M3 state layer overlay. Renders a semi-transparent shape on top of a
// container using the container's content colour as the layer colour.
// Opacities: hover=0.08, focus=0.12, press=0.12, drag=0.16.
class StateLayer {
public:
  StateLayer() = default;

  void setColor(float r, float g, float b) { cr_ = r; cg_ = g; cb_ = b; }
  void setRadius(float rad) { radius_ = rad; }
  void setGeometry(float x, float y, float w, float h) { gx_ = x; gy_ = y; gw_ = w; gh_ = h; }

  void setHovered(bool h) { hovered_ = h; }
  void setPressed(bool p) { pressed_ = p; }
  void setFocused(bool f) { focused_ = f; }
  void setDragged(bool d) { dragged_ = d; }

  [[nodiscard]] bool hovered() const noexcept { return hovered_; }
  [[nodiscard]] bool pressed() const noexcept { return pressed_; }
  [[nodiscard]] bool focused() const noexcept { return focused_; }

  // Current target opacity from M3 spec.
  [[nodiscard]] float targetOpacity() const noexcept {
    if (dragged_) return 0.16f;
    if (pressed_) return 0.12f;
    if (focused_) return 0.12f;
    if (hovered_) return 0.08f;
    return 0.0f;
  }

  // Animate opacity toward target. Call tick() each frame.
  void tick(uint64_t nowMs) { opacityTween_.start(opacityTween_.value(nowMs), targetOpacity(), nowMs, 100, kStandardDecelerate); }

  void paint(cairo_t* cr, uint64_t nowMs = 0) const {
    const float alpha = opacityTween_.active ? opacityTween_.value(nowMs) : targetOpacity();
    if (alpha <= 0.0f || gw_ <= 0 || gh_ <= 0) return;

    cairo_save(cr);
    cairo_translate(cr, gx_, gy_);

    const float r = std::min(radius_, std::min(gw_, gh_) * 0.5f);
    if (r > 0) {
      cairo_new_path(cr);
      cairo_arc(cr, r, r, r, M_PI, 1.5 * M_PI);
      cairo_arc(cr, gw_ - r, r, r, 1.5 * M_PI, 2.0 * M_PI);
      cairo_arc(cr, gw_ - r, gh_ - r, r, 0.0, 0.5 * M_PI);
      cairo_arc(cr, r, gh_ - r, r, 0.5 * M_PI, M_PI);
      cairo_close_path(cr);
    } else {
      cairo_rectangle(cr, 0, 0, gw_, gh_);
    }

    cairo_set_source_rgba(cr, cr_, cg_, cb_, alpha);
    cairo_fill(cr);
    cairo_restore(cr);
  }

private:
  float cr_ = 0.0f, cg_ = 0.0f, cb_ = 0.0f;
  float radius_ = 0.0f;
  float gx_ = 0, gy_ = 0, gw_ = 0, gh_ = 0;
  bool hovered_ = false, pressed_ = false, focused_ = false, dragged_ = false;
  mutable Tween opacityTween_;
};

// M3 focus ring — 2–3dp outline drawn outside component edge with 1dp gap.
class FocusRing {
public:
  FocusRing() = default;

  void setColor(float r, float g, float b) { cr_ = r; cg_ = g; cb_ = b; }
  void setWidth(float w) { width_ = w; }
  void setGap(float g) { gap_ = g; }
  void setRadius(float rad) { radius_ = rad; }
  void setFocused(bool f) { focused_ = f; }

  void paint(cairo_t* cr, float x, float y, float w, float h) const {
    if (!focused_) return;
    const float outerX = x - gap_ - width_;
    const float outerY = y - gap_ - width_;
    const float outerW = w + 2.0f * (gap_ + width_);
    const float outerH = h + 2.0f * (gap_ + width_);
    const float outerR = radius_ + gap_ + width_ * 0.5f;

    cairo_save(cr);
    cairo_set_source_rgba(cr, cr_, cg_, cb_, 1.0f);
    cairo_set_line_width(cr, width_);

    const float r = std::min(outerR, std::min(outerW, outerH) * 0.5f);
    if (r > 0) {
      cairo_new_path(cr);
      cairo_arc(cr, outerX + r, outerY + r, r, M_PI, 1.5 * M_PI);
      cairo_arc(cr, outerX + outerW - r, outerY + r, r, 1.5 * M_PI, 2.0 * M_PI);
      cairo_arc(cr, outerX + outerW - r, outerY + outerH - r, r, 0.0, 0.5 * M_PI);
      cairo_arc(cr, outerX + r, outerY + outerH - r, r, 0.5 * M_PI, M_PI);
      cairo_close_path(cr);
    } else {
      cairo_rectangle(cr, outerX, outerY, outerW, outerH);
    }

    cairo_stroke(cr);
    cairo_restore(cr);
  }

private:
  float cr_ = 0.6f, cg_ = 0.6f, cb_ = 0.6f;  // on-surface-variant default
  float width_ = 2.0f;
  float gap_ = 1.0f;
  float radius_ = 0.0f;
  bool focused_ = false;
};

} // namespace m3
