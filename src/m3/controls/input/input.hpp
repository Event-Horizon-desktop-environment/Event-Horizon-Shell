#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include "m3/core/focus_ring.hpp"
#include "m3/core/glyph.hpp"
#include "m3/core/label.hpp"

namespace m3 {

// M3 Text Field — Filled and Outlined variants, helper text, char counter,
// leading/trailing icons.
class Input {
public:
  enum class Variant { Filled, Outlined };

  Input() = default;

  void setText(std::string_view t) { text_ = t; }
  void setPlaceholder(std::string_view p) { placeholder_ = p; }
  void setHelperText(std::string_view h) { helperText_ = h; }
  void setMaxLength(int max) { maxLength_ = max; }
  void setShowCounter(bool s) { showCounter_ = s; }
  void setVariant(Variant v) { variant_ = v; }
  void setEnabled(bool e) { enabled_ = e; }
  void setFocused(bool f) { focused_ = f; }
  void setHovered(bool h) { hovered_ = h; }
  void setInvalid(bool i) { invalid_ = i; }

  void setLeadingIcon(std::string_view name) { leadingIcon_ = name; }
  void setTrailingIcon(std::string_view name) { trailingIcon_ = name; }

  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }

  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }

  void setOnTextChanged(std::function<void(std::string)> cb) { onTextChanged_ = std::move(cb); }
  void setOnActivate(std::function<void(std::string)> cb) { onActivate_ = std::move(cb); }

  [[nodiscard]] std::string_view text() const noexcept { return text_; }
  [[nodiscard]] bool focused() const noexcept { return focused_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }

  bool containsPoint(float px, float py) const noexcept {
    return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
  }

  void paint(cairo_t* cr) const;
  void paint(cairo_t* cr, uint64_t nowMs);

private:
  void drawFilled(cairo_t* cr, float* outCX) const;
  void drawOutlined(cairo_t* cr, float* outCX) const;
  void drawContent(cairo_t* cr, float contentX, float contentY, float contentW) const;
  void drawHelper(cairo_t* cr) const;

  static constexpr float kHeight = 56.0f;
  static constexpr float kHPad = 16.0f;
  static constexpr float kIconSize = 24.0f;

  Variant variant_ = Variant::Filled;
  std::string text_, placeholder_, helperText_;
  std::string leadingIcon_, trailingIcon_;
  int maxLength_ = 0;
  bool showCounter_ = false;
  bool enabled_ = true, hovered_ = false, focused_ = false, invalid_ = false;

  float x_ = 0, y_ = 0, w_ = 0, h_ = 56;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;

  FocusRing focusRing_;
  std::function<void(std::string)> onTextChanged_;
  std::function<void(std::string)> onActivate_;
};

void Input::paint(cairo_t* cr) const {
  const_cast<Input*>(this)->paint(cr, 0);
}

void Input::paint(cairo_t* cr, uint64_t nowMs) {
  if (w_ <= 0 || h_ <= 0) return;

  const float inputH = kHeight;
  const float iy = y_;

  float contentX = x_;

  if (variant_ == Variant::Filled) {
    drawFilled(cr, &contentX);
  } else {
    drawOutlined(cr, &contentX);
  }

  drawContent(cr, contentX, iy, w_ - (contentX - x_) - kHPad);
  drawHelper(cr);

  // Focus ring
  focusRing_.setFocused(focused_);
  focusRing_.setColor(invalid_ ? 0.91f : accentR_, invalid_ ? 0.42f : accentG_, invalid_ ? 0.35f : accentB_);
  const float fr = variant_ == Variant::Outlined ? 4.0f : 4.0f;
  focusRing_.setRadius(fr);
  focusRing_.paint(cr, x_, iy, w_, inputH);
}

void Input::drawFilled(cairo_t* cr, float* outCX) const {
  const float inputH = kHeight;
  const float activeH = 2.0f;
  const float inactiveH = 1.0f;
  const float r = 4.0f;

  const bool hasLeading = !leadingIcon_.empty();
  *outCX = x_ + kHPad + (hasLeading ? kIconSize + 8.0f : 0);

  cairo_save(cr);

  // Container background (filled style — slightly lighter than surface)
  const float bgA = enabled_ ? 1.0f : 0.38f;
  cairo_set_source_rgba(cr, surfaceR_ * 1.05f, surfaceG_ * 1.05f, surfaceB_ * 1.05f, bgA);
  cairo_new_path(cr);
  cairo_arc(cr, x_ + r, y_ + r, r, M_PI, 1.5 * M_PI);
  cairo_arc(cr, x_ + w_ - r, y_ + r, r, 1.5 * M_PI, 2.0 * M_PI);
  cairo_line_to(cr, x_ + w_, y_ + inputH);
  cairo_line_to(cr, x_, y_ + inputH);
  cairo_close_path(cr);
  cairo_fill(cr);

  // Bottom line
  float lineR, lineG, lineB;
  if (invalid_) { lineR = 0.91f; lineG = 0.42f; lineB = 0.35f; }
  else if (focused_) { lineR = accentR_; lineG = accentG_; lineB = accentB_; }
  else { lineR = outlineR_; lineG = outlineG_; lineB = outlineB_; }

  cairo_set_source_rgba(cr, lineR, lineG, lineB, enabled_ ? 1.0f : 0.38f);
  cairo_set_line_width(cr, focused_ ? activeH : inactiveH);
  cairo_move_to(cr, x_, y_ + inputH);
  cairo_line_to(cr, x_ + w_, y_ + inputH);
  cairo_stroke(cr);

  cairo_restore(cr);
}

void Input::drawOutlined(cairo_t* cr, float* outCX) const {
  const float inputH = kHeight;
  const float r = 4.0f;

  const bool hasLeading = !leadingIcon_.empty();
  *outCX = x_ + kHPad + (hasLeading ? kIconSize + 8.0f : 0);

  cairo_save(cr);

  float borderR, borderG, borderB;
  if (invalid_) { borderR = 0.91f; borderG = 0.42f; borderB = 0.35f; }
  else if (focused_) { borderR = accentR_; borderG = accentG_; borderB = accentB_; }
  else { borderR = outlineR_; borderG = outlineG_; borderB = outlineB_; }

  const float bw = focused_ ? 2.0f : 1.0f;
  cairo_set_source_rgba(cr, borderR, borderG, borderB, enabled_ ? 1.0f : 0.38f);
  cairo_set_line_width(cr, bw);

  cairo_new_path(cr);
  cairo_arc(cr, x_ + r, y_ + r, r - bw * 0.5f, M_PI, 1.5 * M_PI);
  cairo_arc(cr, x_ + w_ - r, y_ + r, r - bw * 0.5f, 1.5 * M_PI, 2.0 * M_PI);
  cairo_arc(cr, x_ + w_ - r, y_ + inputH - r, r - bw * 0.5f, 0.0, 0.5 * M_PI);
  cairo_arc(cr, x_ + r, y_ + inputH - r, r - bw * 0.5f, 0.5 * M_PI, M_PI);
  cairo_close_path(cr);
  cairo_stroke(cr);

  cairo_restore(cr);
}

void Input::drawContent(cairo_t* cr, float contentX, float contentY, float contentW) const {
  const float inputH = kHeight;
  const bool hasText = !text_.empty();

  // Leading icon
  if (!leadingIcon_.empty()) {
    Glyph gl;
    gl.setGlyph(leadingIcon_);
    gl.setSize(kIconSize);
    gl.setColor(textR_, textG_, textB_, enabled_ ? 0.6f : 0.38f);
    gl.paintAt(cr, x_ + kHPad, contentY + (inputH - kIconSize) * 0.5f);
  }

  // Trailing icon (or counter)
  float trailingX = x_ + w_ - kHPad;

  if (!trailingIcon_.empty()) {
    Glyph gl;
    gl.setGlyph(trailingIcon_);
    gl.setSize(kIconSize);
    gl.setColor(textR_, textG_, textB_, enabled_ ? 0.6f : 0.38f);
    gl.paintAt(cr, trailingX - kIconSize, contentY + (inputH - kIconSize) * 0.5f);
    trailingX -= kIconSize + 4;
  }

  // Char counter
  if (showCounter_ && maxLength_ > 0) {
    std::string counter = std::to_string(static_cast<int>(text_.size())) + "/" + std::to_string(maxLength_);
    Label lbl;
    lbl.setText(counter);
    lbl.setFontSize(12);
    lbl.setColor(textR_, textG_, textB_, 0.6f);

    float cw = 0, ch = 0;
    lbl.measureExtents(cw, ch);
    lbl.paintAt(cr, trailingX - cw, contentY + inputH + 4);
  }

  // Text / placeholder
  if (hasText) {
    Label lbl;
    lbl.setText(text_);
    lbl.setFontSize(16);
    lbl.setColor(textR_, textG_, textB_, enabled_ ? 1.0f : 0.38f);
    lbl.paintAt(cr, contentX, contentY + (inputH - 20) * 0.5f);
  } else {
    Label lbl;
    lbl.setText(placeholder_.empty() ? " " : placeholder_);
    lbl.setFontSize(16);
    lbl.setColor(textR_, textG_, textB_, enabled_ ? 0.4f : 0.2f);
    lbl.paintAt(cr, contentX, contentY + (inputH - 20) * 0.5f);
  }

  // Cursor (when focused)
  if (focused_) {
    float cursorX = contentX;
    if (hasText) {
      // Approximate cursor position at end of text
      Label lbl;
      lbl.setText(text_);
      lbl.setFontSize(16);
      float tw = 0, th = 0;
      lbl.measureExtents(tw, th);
      cursorX += tw;
    }
    cairo_save(cr);
    cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 1.0f);
    cairo_set_line_width(cr, 1.5f);
    cairo_move_to(cr, cursorX, contentY + 8);
    cairo_line_to(cr, cursorX, contentY + inputH - 8);
    cairo_stroke(cr);
    cairo_restore(cr);
  }
}

void Input::drawHelper(cairo_t* cr) const {
  if (helperText_.empty()) return;
  Label lbl;
  lbl.setText(helperText_);
  lbl.setFontSize(12);
  lbl.setColor(invalid_ ? 0.91f : textR_, invalid_ ? 0.42f : textG_, invalid_ ? 0.35f : textB_, 0.6f);
  lbl.paintAt(cr, x_ + kHPad, y_ + kHeight + 2);
}

} // namespace m3
