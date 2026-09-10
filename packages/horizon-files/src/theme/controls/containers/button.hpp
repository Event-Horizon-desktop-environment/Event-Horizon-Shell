#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include "theme/core/primitives/box.hpp"
#include "theme/core/focus_ring.hpp"
#include "theme/core/glyph.hpp"
#include "theme/core/label.hpp"
#include "theme/core/primitives/state_layer.hpp"
#include "theme/tokens/elevation.hpp"
#include "theme/tokens/shape.hpp"
#include "theme/tokens/type_scale.hpp"

namespace m3 {

// M3 Button — 4 styles, 5 sizes, state layers, focus ring, toggle mode.
class Button {
public:
  enum class Style { Filled, Tonal, Outlined, Text };
  enum class Size { XS, S, M, L, XL };
  enum class CornerStyle { Pill, Square };  // M3 Expressive

  Button() = default;

  // --- Configuration ---
  void setLabel(std::string_view text) { label_.setText(text); }
  void setGlyph(std::string_view name) { glyph_.setGlyph(name); }
  void setStyle(Style s) { style_ = s; }
  void setSize(Size s) { size_ = s; }
  void setCornerStyle(CornerStyle cs) { cornerStyle_ = cs; }
  void setEnabled(bool e) { enabled_ = e; if (!e) { hovered_ = false; pressed_ = false; } }
  void setToggleMode(bool t) { toggleMode_ = t; }
  void setHovered(bool h) { hovered_ = h; pointerEnabled_ = false; }
  void setPressed(bool p) { pressed_ = p; }
  void setToggled(bool t) { toggled_ = t; }

  // Built-in pointer tracking for auto-hover. Call once per frame before paint().
  // When set, hovered_ is derived automatically from containsPoint() — no external
  // btn_hit lambda or setHovered call needed. Pure float compares, ~0.001µs.
  void setPointer(float px, float py) { pointerX_ = px; pointerY_ = py; pointerEnabled_ = true; }
  void clearPointer() { pointerEnabled_ = false; }

  // Override colours (leave at 0,0,0 to use M3 role defaults).
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }

  // --- Sizing ---
  void setMinSize(float w, float h) { minW_ = w; minH_ = h; }
  void setGeometry(float x, float y, float w, float h) {
    x_ = x; y_ = y;
    w_ = std::max(w, minW_);
    h_ = std::max(h, minH_);
    if (!label_.text().empty()) {
      label_.setFontSize(fontForSize());
      float lw = 0, lh = 0;
      label_.measureExtents(lw, lh);
      float needW = lw + 2.0f * kAutoPad;
      if (minH_ > 0) h_ = std::max(h_, heightForSize());
      if (w_ < needW) {
        float dw = needW - w_;
        w_ = needW;
        x_ -= dw * 0.5f;
      }
    }
  }

  [[nodiscard]] Style style() const noexcept { return style_; }
  [[nodiscard]] Size size() const noexcept { return size_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] bool toggled() const noexcept { return toggled_; }
  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }
  [[nodiscard]] bool hovered() const noexcept { return hovered_; }
  [[nodiscard]] bool pressed() const noexcept { return pressed_; }

  [[nodiscard]] float minWidth() const noexcept { return minW_; }
  [[nodiscard]] float minHeight() const noexcept { return minH_; }

  // --- Hit testing ---
  bool containsPoint(float px, float py) const noexcept {
    return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
  }

  // --- Event handlers ---
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
    if (onPress_) onPress_();
    return true;
  }

  bool handlePointerUp(float px, float py) {
    if (!enabled_) return false;
    const bool wasPressed = pressed_;
    pressed_ = false;
    if (toggleMode_ && wasPressed && containsPoint(px, py)) {
      toggled_ = !toggled_;
    }
    if (wasPressed && containsPoint(px, py)) {
      if (onClick_) onClick_();
    }
    if (onRelease_) onRelease_();
    return wasPressed;
  }

  // --- Callbacks ---
  void setOnClick(std::function<void()> cb) { onClick_ = std::move(cb); }
  void setOnPress(std::function<void()> cb) { onPress_ = std::move(cb); }
  void setOnRelease(std::function<void()> cb) { onRelease_ = std::move(cb); }

  // --- Rendering ---
  void paint(cairo_t* cr, uint64_t nowMs = 0) {
    if (w_ <= 0.0f || h_ <= 0.0f) return;

    if (pointerEnabled_)
        hovered_ = enabled_ && containsPoint(pointerX_, pointerY_);

    stateLayer_.setGeometry(x_, y_, w_, h_);
    stateLayer_.setRadius(radius());
    stateLayer_.setHovered(hovered_);
    stateLayer_.setPressed(pressed_);
    stateLayer_.tick(nowMs);

    resolveColors();
    drawContainer(cr);
    stateLayer_.paint(cr, nowMs);
    drawContent(cr);
    drawFocusRing(cr);
  }

  void paint(cairo_t* cr) const {
    // Non-animated paint — uses current state directly.
    if (w_ <= 0.0f || h_ <= 0.0f) return;

    const_cast<Button*>(this)->paint(cr, 0);
  }

private:
  // --- Size constants ---
  static constexpr float kSizeHeight[] = { 24.0f, 32.0f, 40.0f, 48.0f, 56.0f };
  static constexpr float kSizeHPad[]  = { 16.0f, 16.0f, 24.0f, 24.0f, 24.0f };
  static constexpr float kSizeFont[]  = { 12.0f, 13.0f, 14.0f, 16.0f, 16.0f };
  static constexpr float kAutoPad     = 10.0f;

  [[nodiscard]] int sizeIndex() const noexcept {
    return static_cast<int>(size_);
  }

  [[nodiscard]] float heightForSize() const noexcept { return kSizeHeight[sizeIndex()]; }
  [[nodiscard]] float hPadForSize() const noexcept { return kSizeHPad[sizeIndex()]; }
  [[nodiscard]] float fontForSize() const noexcept { return kSizeFont[sizeIndex()]; }

  [[nodiscard]] float radius() const noexcept {
    if (cornerStyle_ == CornerStyle::Square) return 12.0f;
    return std::min(w_, h_) * 0.5f;  // pill
  }

  void resolveColors() {
    if (accentR_ != 0 || accentG_ != 0 || accentB_ != 0) return; // manual override

    // Default M3 role-based colours derived from accent/surface.
    // In a full theme system these come from ColorRole lookup.
    // For now, use the existing 3-role system.
    // (To be replaced with m3::Palette::colorForRole() in Sprint 5.)
  }

  void drawContainer(cairo_t* cr) {
    const float r = radius();
    const float alpha = enabled_ ? 1.0f : 0.12f;

    float bgR = 0, bgG = 0, bgB = 0, bgA = 0;
    float fgR = 0, fgG = 0, fgB = 0;

    auto blend = [](float a, float b, float t) { return a + (b - a) * t; };

    switch (style_) {
      case Style::Filled:
        if (toggled_) {
          bgR = 0.12f; bgG = 0.12f; bgB = 0.14f; bgA = enabled_ ? 1.0f : alpha;
          fgR = 0.43f; fgG = 0.59f; fgB = 0.88f;
        } else {
          bgR = accentR_; bgG = accentG_; bgB = accentB_; bgA = enabled_ ? 1.0f : alpha;
          fgR = 1.0f; fgG = 1.0f; fgB = 1.0f;
        }
        break;
      case Style::Tonal: {
        const float cr = blend(accentR_, surfaceR_, 0.55f);
        const float cg = blend(accentG_, surfaceG_, 0.55f);
        const float cb = blend(accentB_, surfaceB_, 0.55f);
        bgR = cr; bgG = cg; bgB = cb; bgA = enabled_ ? 1.0f : alpha;
        fgR = surfaceR_; fgG = surfaceG_; fgB = surfaceB_;
        break;
      }
      case Style::Outlined:
        bgR = 0; bgG = 0; bgB = 0; bgA = 0;
        fgR = accentR_; fgG = accentG_; fgB = accentB_;
        break;
      case Style::Text:
        bgR = 0; bgG = 0; bgB = 0; bgA = 0;
        fgR = accentR_; fgG = accentG_; fgB = accentB_;
        break;
    }

    // Container fill
    // For Outlined/Text we still want a subtle glassy backing so the inner frosted effect is visible on buttons
    float contR = bgR, contG = bgG, contB = bgB, contA = bgA;
    if ((style_ == Style::Outlined || style_ == Style::Text) && contA < 0.05f) {
        contR = surfaceR_;
        contG = surfaceG_;
        contB = surfaceB_;
        contA = 0.08f;
    }
    Box container;
    container.setColor(contR, contG, contB, enabled_ ? contA : 0.06f);
    container.setRadius(r);
    container.setGeometry(x_, y_, w_, h_);
    container.setGlassy(true);
    container.paint(cr);

    // Outline
    if (style_ == Style::Outlined) {
      cairo_save(cr);
      const float oR = outlineR_ ? outlineR_ : 0.35f;
      const float oG = outlineG_ ? outlineG_ : 0.35f;
      const float oB = outlineB_ ? outlineB_ : 0.37f;
      cairo_set_source_rgba(cr, oR, oG, oB, enabled_ ? 1.0f : 0.12f);
      cairo_set_line_width(cr, 1.0f);
      if (r > 0) {
        cairo_new_path(cr);
        cairo_arc(cr, x_ + r, y_ + r, r - 0.5f, M_PI, 1.5 * M_PI);
        cairo_arc(cr, x_ + w_ - r, y_ + r, r - 0.5f, 1.5 * M_PI, 2.0 * M_PI);
        cairo_arc(cr, x_ + w_ - r, y_ + h_ - r, r - 0.5f, 0.0, 0.5 * M_PI);
        cairo_arc(cr, x_ + r, y_ + h_ - r, r - 0.5f, 0.5 * M_PI, M_PI);
        cairo_close_path(cr);
      } else {
        cairo_rectangle(cr, x_, y_, w_, h_);
      }
      cairo_stroke(cr);
      cairo_restore(cr);
    }

    // Store resolved fg colour for content
    const_cast<Button*>(this)->resolvedFgR_ = fgR;
    const_cast<Button*>(this)->resolvedFgG_ = fgG;
    const_cast<Button*>(this)->resolvedFgB_ = fgB;

    // State layer uses content colour
    stateLayer_.setColor(fgR, fgG, fgB);
  }

  void drawContent(cairo_t* cr) {
    const float fgA = enabled_ ? 1.0f : 0.38f;
    const bool hasGlyph = !glyph_.glyph().empty();
    const bool hasLabel = !label_.text().empty();

    if (!hasGlyph && !hasLabel) return;

    cairo_save(cr);
    cairo_translate(cr, x_, y_);

    const float iconSize = 20.0f;

    if (hasGlyph) {
      glyph_.setSize(iconSize);
      glyph_.setColor(resolvedFgR_, resolvedFgG_, resolvedFgB_, fgA);

      if (hasLabel) {
        float iw = 0, ih = 0;
        glyph_.measureExtents(iw, ih);
        const float gap = 6.0f;

        label_.setFontSize(fontForSize());
        float lw = 0, lh = 0;
        label_.measureExtents(lw, lh);

        const float totalW = iw + gap + lw;
        const float baseX = std::round((w_ - totalW) * 0.5f);
        const float iy = std::round((h_ - ih) * 0.5f);
        const float ly = std::round((h_ - lh) * 0.5f);

        glyph_.paintAt(cr, baseX, iy);
        label_.setColor(resolvedFgR_, resolvedFgG_, resolvedFgB_, fgA);
        label_.paintAt(cr, baseX + iw + gap, ly);
      } else {
        float iw = 0, ih = 0;
        glyph_.measureExtents(iw, ih);
        const float gx = std::round((w_ - iw) * 0.5f);
        const float gy = std::round((h_ - ih) * 0.5f);
        glyph_.paintAt(cr, gx, gy);
      }
    } else if (hasLabel) {
      label_.setFontSize(fontForSize());
      label_.setColor(resolvedFgR_, resolvedFgG_, resolvedFgB_, fgA);

      float lw = 0, lh = 0;
      label_.measureExtents(lw, lh);
      const float lx = std::round((w_ - lw) * 0.5f);
      const float ly = std::round((h_ - lh) * 0.5f);
      label_.setColor(0, 0, 0, fgA * 0.35f);
      label_.paintAt(cr, lx + 1.0f, ly + 1.0f);
      label_.setColor(resolvedFgR_, resolvedFgG_, resolvedFgB_, fgA);
      label_.paintAt(cr, lx, ly);
    }

    cairo_restore(cr);
  }

  void drawFocusRing(cairo_t* cr) {
    if (!focused_) return;
    focusRing_.setFocused(true);
    focusRing_.setColor(outlineR_ ? outlineR_ : 0.6f,
                         outlineG_ ? outlineG_ : 0.6f,
                         outlineB_ ? outlineB_ : 0.6f);
    focusRing_.setRadius(radius());
    focusRing_.paint(cr, x_, y_, w_, h_);
  }

  // State
  Style style_ = Style::Filled;
  Size size_ = Size::M;
  CornerStyle cornerStyle_ = CornerStyle::Pill;
  bool enabled_ = true;
  bool hovered_ = false;
  bool pressed_ = false;
  bool focused_ = false;
  bool toggleMode_ = false;
  bool toggled_ = false;

  // Geometry
  float x_ = 0.0f, y_ = 0.0f, w_ = 0.0f, h_ = 0.0f;
  float minW_ = 64.0f, minH_ = 36.0f;

  // Pointer tracking for auto-hover
  float pointerX_ = 0.0f, pointerY_ = 0.0f;
  bool pointerEnabled_ = false;

  // Colour overrides (0,0,0 = use M3 role defaults)
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.0f, outlineG_ = 0.0f, outlineB_ = 0.0f;

  // Resolved fg for content rendering
  float resolvedFgR_ = 1.0f, resolvedFgG_ = 1.0f, resolvedFgB_ = 1.0f;

  // Components
  Label label_;
  Glyph glyph_;
  StateLayer stateLayer_;
  FocusRing focusRing_;

  // Callbacks
  std::function<void()> onClick_;
  std::function<void()> onPress_;
  std::function<void()> onRelease_;
};

} // namespace m3
