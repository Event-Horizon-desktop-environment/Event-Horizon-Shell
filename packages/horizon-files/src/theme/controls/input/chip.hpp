#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <cairo/cairo.h>

#include "theme/core/animation.hpp"
#include "theme/core/focus_ring.hpp"
#include "theme/core/glyph.hpp"
#include "theme/core/label.hpp"
#include "theme/core/primitives/box.hpp"
#include "theme/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Chip — 4 variants: Assist, Filter, Input, Suggestion.
// Optional leading icon, trailing icon, close icon.
// Filter variant supports toggle selection.
class Chip {
public:
  enum class Variant { Assist, Filter, Input, Suggestion };

  Chip() = default;

  void setVariant(Variant v) { variant_ = v; }
  void setLabel(std::string_view t) { label_.setText(t); }
  void setLeadingIcon(std::string_view g) { leadingGlyph_.setGlyph(g); }
  void setTrailingIcon(std::string_view g) { trailingGlyph_.setGlyph(g); }
  void setCloseIcon(std::string_view g) { closeGlyph_.setGlyph(g); }

  void setSelected(bool s, uint64_t nowMs = 0) {
    if (s == selected_) return;
    selected_ = s;
    if (variant_ == Variant::Filter) {
      selectTween_.start(selectTween_.value(nowMs), s ? 1.0f : 0.0f, nowMs, 200, kEmphasized);
    }
  }

  void setEnabled(bool e) { enabled_ = e; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }
  void setHovered(bool h) { hovered_ = h; }
  void setPressed(bool p) { pressed_ = p; }
  void setFocused(bool f) { focused_ = f; }

  void setOnClick(std::function<void()> cb) { onClick_ = std::move(cb); }
  void setOnToggle(std::function<void(bool)> cb) { onToggle_ = std::move(cb); }

  [[nodiscard]] bool selected() const noexcept { return selected_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] Variant variant() const noexcept { return variant_; }

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
      if (variant_ == Variant::Filter) {
        selected_ = !selected_;
        if (onToggle_) onToggle_(selected_);
      }
      if (onClick_) onClick_();
    }
    return true;
  }

  void paint(cairo_t* cr, uint64_t nowMs = 0) {
    if (w_ <= 0 || h_ <= 0) return;

    const float r = 8.0f;          // corner.small
    const float pad = 16.0f;       // horizontal padding
    const float iconPad = 8.0f;    // gap between icon and text
    const float iconSize = 18.0f;
    const float closeSize = 18.0f;
    const float alpha = enabled_ ? 1.0f : 0.38f;

    // ---- Resolve colours from variant ----
    const float selectT = variant_ == Variant::Filter ? selectTween_.value(nowMs) : 0.0f;

    float bgR, bgG, bgB, bgA;
    float fgR, fgG, fgB;
    bool outlined = false;

    if (!enabled_) {
      bgR = surfaceR_; bgG = surfaceG_; bgB = surfaceB_; bgA = 0.12f;
      fgR = textR_; fgG = textG_; fgB = textB_;
    } else switch (variant_) {
      case Variant::Assist:
        // surface-container-low
        bgR = blend(surfaceR_, outlineR_, 0.08f); bgG = blend(surfaceG_, outlineG_, 0.08f);
        bgB = blend(surfaceB_, outlineB_, 0.08f); bgA = 1.0f;
        fgR = textR_; fgG = textG_; fgB = textB_;
        break;
      case Variant::Filter: {
        // Unselected: surface-container-high, Selected: secondary-container
        const float t = selectT;
        bgR = blend(accentR_ * 0.3f, accentR_ * 0.7f, t);
        bgG = blend(accentG_ * 0.3f, accentG_ * 0.7f, t);
        bgB = blend(accentB_ * 0.3f, accentB_ * 0.7f, t);
        bgA = 1.0f;
        fgR = blend(textR_, 1.0f, t);
        fgG = blend(textG_, 1.0f, t);
        fgB = blend(textB_, 1.0f, t);
        break;
      }
      case Variant::Input:
        bgR = blend(surfaceR_, outlineR_, 0.15f); bgG = blend(surfaceG_, outlineG_, 0.15f);
        bgB = blend(surfaceB_, outlineB_, 0.15f); bgA = 1.0f;
        fgR = textR_; fgG = textG_; fgB = textB_;
        outlined = true;
        break;
      case Variant::Suggestion:
        bgR = blend(surfaceR_, outlineR_, 0.15f); bgG = blend(surfaceG_, outlineG_, 0.15f);
        bgB = blend(surfaceB_, outlineB_, 0.15f); bgA = 1.0f;
        fgR = textR_; fgG = textG_; fgB = textB_;
        break;
    }

    cairo_save(cr);

    // Background via Box (applies glassy inner rim, using chip's native 8px small radius for conformance)
    {
      Box bgBox;
      bgBox.setColor(bgR, bgG, bgB, bgA * alpha);
      bgBox.setRadius(r);
      bgBox.setGeometry(x_, y_, w_, h_);
      bgBox.setGlassy(true);
      bgBox.paint(cr);
    }

    cairo_translate(cr, x_, y_);

    // Outline (Input variant)
    if (outlined && enabled_) {
      cairo_new_path(cr);
      cairo_arc(cr, r, r, r - 0.5f, M_PI, 1.5 * M_PI);
      cairo_arc(cr, w_ - r, r, r - 0.5f, 1.5 * M_PI, 2.0 * M_PI);
      cairo_arc(cr, w_ - r, h_ - r, r - 0.5f, 0.0, 0.5 * M_PI);
      cairo_arc(cr, r, h_ - r, r - 0.5f, 0.5 * M_PI, M_PI);
      cairo_close_path(cr);
      cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 1.0f);
      cairo_set_line_width(cr, 1.0f);
      cairo_stroke(cr);
    }

    // Content layout
    const float cy = h_ * 0.5f;
    float cx = pad;

    // Leading icon
    if (!leadingGlyph_.glyph().empty()) {
      leadingGlyph_.setSize(iconSize);
      leadingGlyph_.setColor(fgR, fgG, fgB, alpha * 0.78f);
      leadingGlyph_.paintAt(cr, cx, cy - iconSize * 0.5f);
      cx += iconSize + iconPad;
    }

    // Label
    float lw = 0, lh = 0;
    label_.measureExtents(lw, lh);
    label_.setColor(fgR, fgG, fgB, alpha);
    label_.paintAt(cr, cx, cy - lh * 0.5f);
    cx += lw;

    // Trailing icon
    if (!trailingGlyph_.glyph().empty()) {
      cx += iconPad;
      trailingGlyph_.setSize(iconSize);
      trailingGlyph_.setColor(fgR, fgG, fgB, alpha * 0.78f);
      trailingGlyph_.paintAt(cr, cx, cy - iconSize * 0.5f);
      cx += iconSize;
    }

    // Close icon (rightmost)
    if (!closeGlyph_.glyph().empty()) {
      cx += iconPad;
      closeGlyph_.setSize(closeSize);
      closeGlyph_.setColor(fgR, fgG, fgB, alpha * 0.78f);
      closeGlyph_.paintAt(cr, cx, cy - closeSize * 0.5f);
    }

    cairo_restore(cr);

    // State layer
    stateLayer_.setColor(fgR, fgG, fgB);
    stateLayer_.setRadius(r);
    stateLayer_.setGeometry(x_, y_, w_, h_);
    stateLayer_.setHovered(hovered_);
    stateLayer_.setPressed(pressed_);
    stateLayer_.setFocused(focused_);
    stateLayer_.tick(nowMs);
    stateLayer_.paint(cr, nowMs);

    // Focus ring
    focusRing_.setFocused(focused_);
    focusRing_.setColor(outlineR_, outlineG_, outlineB_);
    focusRing_.setRadius(r);
    focusRing_.paint(cr, x_, y_, w_, h_);
  }

  void paint(cairo_t* cr) const {
    const_cast<Chip*>(this)->paint(cr, 0);
  }

private:
  static float blend(float a, float b, float t) {
    return a + (b - a) * t;
  }

  Variant variant_ = Variant::Assist;
  bool selected_ = false;
  bool enabled_ = true;
  bool hovered_ = false, pressed_ = false, focused_ = false;

  float x_ = 0, y_ = 0, w_ = 0, h_ = 32;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;

  Label label_;
  Glyph leadingGlyph_;
  Glyph trailingGlyph_;
  Glyph closeGlyph_;
  StateLayer stateLayer_;
  FocusRing focusRing_;
  Tween selectTween_;

  std::function<void()> onClick_;
  std::function<void(bool)> onToggle_;
};

} // namespace m3
