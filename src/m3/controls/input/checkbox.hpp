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

// M3 Checkbox — 3 states (checked/unchecked/indeterminate), state layer,
// focus ring, animated checkmark.
class Checkbox {
public:
  enum class State { Unchecked, Checked, Indeterminate };

  Checkbox() = default;

  void setState(State s, uint64_t nowMs = 0) {
    if (s == state_) return;
    state_ = s;
    animTween_.start(animTween_.value(nowMs), s == State::Checked ? 1.0f : 0.0f, nowMs, 200, kEmphasized);
  }

  void setEnabled(bool e) { enabled_ = e; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setHovered(bool h) { hovered_ = h; }
  void setPressed(bool p) { pressed_ = p; }
  void setFocused(bool f) { focused_ = f; }

  void setOnClick(std::function<void()> cb) { onClick_ = std::move(cb); }
  void setOnChange(std::function<void(State)> cb) { onChange_ = std::move(cb); }

  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] bool checked() const noexcept { return state_ == State::Checked; }
  [[nodiscard]] bool indeterminate() const noexcept { return state_ == State::Indeterminate; }

  bool containsPoint(float px, float py) const noexcept {
    return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
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
      State next;
      if (state_ == State::Unchecked) next = State::Checked;
      else if (state_ == State::Checked) next = State::Unchecked;
      else next = State::Unchecked;
      setState(next, 0);
      if (onChange_) onChange_(state_);
      if (onClick_) onClick_();
    }
    return true;
  }

  void paint(cairo_t* cr) { paint(cr, 0); }

  void paint(cairo_t* cr, uint64_t nowMs) {
    if (w_ <= 0 || h_ <= 0) return;

    const float cx = x_ + w_ * 0.5f;
    const float cy = y_ + h_ * 0.5f;
    const float boxSize = 18.0f;
    const float halfBox = boxSize * 0.5f;
    const float bx = cx - halfBox;
    const float by = cy - halfBox;
    const float radius = 4.0f;  // corner.extra-small

    const bool isChecked = state_ == State::Checked;
    const bool isIndet = state_ == State::Indeterminate;
    const float animT = animTween_.value(nowMs);

    // Colors
    float bgR, bgG, bgB, bgA;
    float fgR = 1.0f, fgG = 1.0f, fgB = 1.0f;
    float outR, outG, outB, outA;

    if (!enabled_) {
      bgR = textR_; bgG = textR_; bgB = textR_; bgA = 0.38f;
      outR = textR_; outG = textR_; outB = textR_; outA = 0.38f;
    } else if (isChecked || isIndet) {
      bgR = accentR_; bgG = accentG_; bgB = accentB_; bgA = 1.0f;
      outR = accentR_; outG = accentG_; outB = accentB_; outA = 0;
    } else {
      bgR = 0; bgG = 0; bgB = 0; bgA = 0;
      outR = textR_; outG = textG_; outB = textB_; outA = 0.5f;
    }

    // Animate fill for checked/unchecked transition
    float fillR, fillG, fillB, fillA;
    if (!enabled_) {
      fillR = bgR; fillG = bgG; fillB = bgB; fillA = bgA;
    } else if (isChecked || isIndet) {
      fillR = accentR_; fillG = accentG_; fillB = accentB_; fillA = animT;
    } else {
      fillR = 0; fillG = 0; fillB = 0; fillA = (1.0f - animT) * 0.0f;
    }

    cairo_save(cr);

    // Container
    cairo_new_path(cr);
    cairo_arc(cr, bx + radius, by + radius, radius, M_PI, 1.5 * M_PI);
    cairo_arc(cr, bx + boxSize - radius, by + radius, radius, 1.5 * M_PI, 2.0 * M_PI);
    cairo_arc(cr, bx + boxSize - radius, by + boxSize - radius, radius, 0.0, 0.5 * M_PI);
    cairo_arc(cr, bx + radius, by + boxSize - radius, radius, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);

    // Fill
    if (fillA > 0) {
      cairo_set_source_rgba(cr, fillR, fillG, fillB, fillA);
      cairo_fill_preserve(cr);
    }

    // Outline
    if (!enabled_) {
      cairo_set_source_rgba(cr, outR, outG, outB, 0.38f);
    } else if (!isChecked && !isIndet) {
      cairo_set_source_rgba(cr, outR, outG, outB, outA);
    }
    if (!isChecked && !isIndet) {
      cairo_set_line_width(cr, 2.0f);
      cairo_new_path(cr);
      cairo_arc(cr, bx + radius, by + radius, radius, M_PI, 1.5 * M_PI);
      cairo_arc(cr, bx + boxSize - radius, by + radius, radius, 1.5 * M_PI, 2.0 * M_PI);
      cairo_arc(cr, bx + boxSize - radius, by + boxSize - radius, radius, 0.0, 0.5 * M_PI);
      cairo_arc(cr, bx + radius, by + boxSize - radius, radius, 0.5 * M_PI, M_PI);
      cairo_close_path(cr);
      cairo_stroke(cr);
    }

    cairo_restore(cr);

    // Glyph: checkmark (checked) or dash (indeterminate)
    if (isChecked || isIndet) {
      cairo_save(cr);
      cairo_set_source_rgba(cr, fgR, fgG, fgB, animT);
      cairo_set_line_width(cr, 2.0f);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

      if (isIndet) {
        // Minus/dash
        cairo_move_to(cr, cx - 5, cy);
        cairo_line_to(cr, cx + 5, cy);
      } else if (animT > 0.3f) {
        // Checkmark (animated draw)
        const float drawT = std::min(1.0f, (animT - 0.3f) / 0.7f);
        const float lx1 = cx - 5, ly1 = cy;
        const float lx2 = cx - 1, ly2 = cy + 4;
        const float lx3 = cx + 6, ly3 = cy - 3;

        // Draw first segment, then second
        const float segLen = std::sqrt(25.0f + 16.0f);
        const float totalLen = segLen + std::sqrt(49.0f + 49.0f);
        const float len1 = segLen / totalLen;
        const float t = drawT;

        if (t < len1) {
          const float st = t / len1;
          cairo_move_to(cr, lx1, ly1);
          cairo_line_to(cr, lx1 + (lx2 - lx1) * st, ly1 + (ly2 - ly1) * st);
        } else {
          const float st = (t - len1) / (1.0f - len1);
          cairo_move_to(cr, lx1, ly1);
          cairo_line_to(cr, lx2, ly2);
          cairo_move_to(cr, lx2, ly2);
          cairo_line_to(cr, lx2 + (lx3 - lx2) * st, ly2 + (ly3 - ly2) * st);
        }
      }
      cairo_stroke(cr);
      cairo_restore(cr);
    }

    // State layer
    stateLayer_.setColor(accentR_, accentG_, accentB_);
    stateLayer_.setRadius(boxSize * 0.5f);
    stateLayer_.setGeometry(cx - boxSize, cy - boxSize, boxSize * 2, boxSize * 2);
    stateLayer_.setHovered(hovered_);
    stateLayer_.setPressed(pressed_);
    stateLayer_.setFocused(focused_);
    stateLayer_.tick(nowMs);
    stateLayer_.paint(cr, nowMs);

    // Focus ring
    focusRing_.setFocused(focused_);
    focusRing_.setColor(textR_, textG_, textB_);
    focusRing_.setRadius(boxSize * 0.5f + 4);
    focusRing_.paint(cr, cx - halfBox - 4, cy - halfBox - 4, boxSize + 8, boxSize + 8);
  }

private:
  State state_ = State::Unchecked;
  bool enabled_ = true;
  bool hovered_ = false, pressed_ = false, focused_ = false;

  float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;

  Tween animTween_;
  StateLayer stateLayer_;
  FocusRing focusRing_;

  std::function<void()> onClick_;
  std::function<void(State)> onChange_;
};

} // namespace m3
