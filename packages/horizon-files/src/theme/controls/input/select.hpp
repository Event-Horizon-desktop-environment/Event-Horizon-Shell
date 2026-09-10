#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <cairo/cairo.h>

#include "theme/core/primitives/box.hpp"
#include "theme/core/focus_ring.hpp"
#include "theme/core/glyph.hpp"
#include "theme/core/label.hpp"
#include "theme/core/primitives/state_layer.hpp"
#include "theme/tokens/elevation.hpp"
#include "theme/tokens/shape.hpp"

namespace m3 {

// M3 Select (Dropdown) — trigger with caret, popup menu, state layers.
class Select {
public:
  Select() = default;

  void setOptions(std::vector<std::string> items) { options_ = std::move(items); sel_ = std::min(sel_, options_.size() - 1); }
  void setSelectedIndex(std::size_t idx) { sel_ = idx; }
  void setPlaceholder(std::string_view text) { placeholder_ = text; }
  void setEnabled(bool e) { enabled_ = e; if (!e) { open_ = false; } }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setMaxVisible(std::size_t rows) { maxVisible_ = rows; }

  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }

  void setOnSelect(std::function<void(std::size_t, std::string_view)> cb) { onSelect_ = std::move(cb); }

  void setTriggerHovered(bool h) { trigHovered_ = h; }
  void setTriggerPressed(bool p) { trigPressed_ = p; }
  void setFocused(bool f) { focused_ = f; }

  [[nodiscard]] std::size_t selectedIndex() const noexcept { return sel_; }
  [[nodiscard]] std::string_view selectedText() const noexcept;
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] bool open() const noexcept { return open_; }
  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }
  [[nodiscard]] float menuHeight() const noexcept;

  bool containsPoint(float px, float py) const noexcept;
  bool containsMenuPoint(float px, float py) const noexcept;

  void close() { open_ = false; }
  void toggle() { if (enabled_) open_ = !open_; }

  bool handlePointerEnter(float, float);
  bool handlePointerLeave();
  bool handlePointerDown(float px, float py);
  bool handlePointerMove(float px, float py);
  bool handlePointerUp(float px, float py);

  void paint(cairo_t* cr) const;

private:
  static constexpr std::size_t kNpos = static_cast<std::size_t>(-1);
  static constexpr float kMenuElevation = 3.0f;  // elevation.level2
  static constexpr float kMenuRadius = 4.0f;      // corner.extra-small

  void selectIndex(std::size_t idx);
  [[nodiscard]] std::size_t optionAt(float px, float py) const noexcept;
  [[nodiscard]] float dropTop() const noexcept;

  void paintTrigger(cairo_t* cr) const;
  void paintMenu(cairo_t* cr) const;

  std::vector<std::string> options_;
  std::size_t sel_ = kNpos;
  std::size_t hoveredIdx_ = kNpos;
  std::string placeholder_;
  bool enabled_ = true, open_ = false;
  bool trigHovered_ = false, trigPressed_ = false;
  bool focused_ = false;

  float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
  float optH_ = 32.0f;
  float padding_ = 12.0f;
  float fontSize_ = 14.0f;
  std::size_t maxVisible_ = 6;

  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;

  StateLayer stateLayer_;
  FocusRing focusRing_;
  std::function<void(std::size_t, std::string_view)> onSelect_;
};

// --- inline impl ---

std::string_view Select::selectedText() const noexcept {
  if (sel_ >= options_.size()) return {};
  return options_[sel_];
}

float Select::menuHeight() const noexcept {
  const std::size_t n = std::min(options_.size(), maxVisible_);
  return static_cast<float>(n) * optH_;
}

bool Select::containsPoint(float px, float py) const noexcept {
  return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
}

bool Select::containsMenuPoint(float px, float py) const noexcept {
  if (!open_) return false;
  const float my = dropTop();
  const float mh = menuHeight();
  return px >= x_ && px < x_ + w_ && py >= my && py < my + mh;
}

float Select::dropTop() const noexcept {
  return y_ + h_ + 2.0f;
}

std::size_t Select::optionAt(float px, float py) const noexcept {
  if (!open_ || options_.empty()) return kNpos;
  const float my = dropTop();
  const int idx = static_cast<int>((py - my) / optH_);
  if (idx < 0 || static_cast<std::size_t>(idx) >= options_.size()) return kNpos;
  return static_cast<std::size_t>(idx);
}

void Select::selectIndex(std::size_t idx) {
  if (idx >= options_.size()) return;
  sel_ = idx;
  open_ = false;
  if (onSelect_) onSelect_(idx, options_[idx]);
}

bool Select::handlePointerEnter(float, float) {
  if (!enabled_) return false;
  trigHovered_ = true;
  return true;
}

bool Select::handlePointerLeave() {
  trigHovered_ = false;
  trigPressed_ = false;
  return true;
}

bool Select::handlePointerDown(float px, float py) {
  if (!enabled_) return false;
  if (containsMenuPoint(px, py)) {
    const std::size_t idx = optionAt(px, py);
    if (idx != kNpos) selectIndex(idx);
    return true;
  }
  if (containsPoint(px, py)) {
    trigPressed_ = true;
    return true;
  }
  return false;
}

bool Select::handlePointerMove(float px, float py) {
  if (!enabled_) return false;
  if (open_) {
    hoveredIdx_ = optionAt(px, py);
  }
  return true;
}

bool Select::handlePointerUp(float, float) {
  if (!enabled_) return false;
  if (trigPressed_) {
    trigPressed_ = false;
    open_ = !open_;
    return true;
  }
  return false;
}

void Select::paint(cairo_t* cr) const {
  paintTrigger(cr);
  if (open_) paintMenu(cr);
}

void Select::paintTrigger(cairo_t* cr) const {
  const float inputH = h_;
  const float r = 4.0f;

  cairo_save(cr);

  // Background (use Box for glassy inner design, radius-conformant at extra-small 4px)
  const float bgA = enabled_ ? 1.0f : 0.38f;
  Box bgBox;
  bgBox.setColor(surfaceR_ * 1.05f, surfaceG_ * 1.05f, surfaceB_ * 1.05f, bgA);
  bgBox.setRadius(r);
  bgBox.setGeometry(x_, y_, w_, inputH);
  bgBox.setGlassy(true);
  bgBox.paint(cr);

  // Bottom line
  float lineR = focused_ ? accentR_ : outlineR_;
  float lineG = focused_ ? accentG_ : outlineG_;
  float lineB = focused_ ? accentB_ : outlineB_;
  cairo_set_source_rgba(cr, lineR, lineG, lineB, enabled_ ? 1.0f : 0.38f);
  cairo_set_line_width(cr, focused_ ? 2.0f : 1.0f);
  cairo_move_to(cr, x_, y_ + inputH);
  cairo_line_to(cr, x_ + w_, y_ + inputH);
  cairo_stroke(cr);

  cairo_restore(cr);

  // Label
  if (sel_ < options_.size()) {
    Label lbl;
    lbl.setText(options_[sel_]);
    lbl.setFontSize(fontSize_);
    lbl.setColor(textR_, textG_, textB_, enabled_ ? 1.0f : 0.38f);
    lbl.paintAt(cr, x_ + padding_, y_ + (inputH - 18) * 0.5f);
  } else {
    Label lbl;
    lbl.setText(placeholder_.empty() ? "Select..." : placeholder_);
    lbl.setFontSize(fontSize_);
    lbl.setColor(textR_, textG_, textB_, enabled_ ? 0.4f : 0.2f);
    lbl.paintAt(cr, x_ + padding_, y_ + (inputH - 18) * 0.5f);
  }

  // Caret
  Glyph caret;
  caret.setGlyph(open_ ? "expand_less" : "expand_more");
  caret.setSize(20);
  caret.setColor(textR_, textG_, textB_, enabled_ ? 1.0f : 0.38f);
  caret.paintAt(cr, x_ + w_ - padding_ - 20, y_ + (inputH - 20) * 0.5f);

  // State layer
  stateLayer_.setColor(textR_, textG_, textB_);
  stateLayer_.setRadius(r);
  stateLayer_.setGeometry(x_, y_, w_, inputH);
  stateLayer_.setHovered(trigHovered_);
  stateLayer_.setPressed(trigPressed_);
  stateLayer_.setFocused(focused_);
  stateLayer_.paint(cr, 0);

  // Focus ring
  focusRing_.setFocused(focused_);
  focusRing_.setColor(accentR_, accentG_, accentB_);
  focusRing_.setRadius(r);
  focusRing_.paint(cr, x_, y_, w_, inputH);
}

void Select::paintMenu(cairo_t* cr) const {
  const float my = dropTop();
  const std::size_t n = std::min(options_.size(), maxVisible_);
  const float mh = static_cast<float>(n) * optH_;

  cairo_save(cr);

  // Menu shadow
  cairo_set_source_rgba(cr, 0, 0, 0, 0.2f);
  cairo_set_line_width(cr, 0); // shadow approximation via fill offset
  // Note: real shadow requires Cairo shadow filter. For now use offset rect.
  cairo_rectangle(cr, x_ + 2, my + 2, w_, mh);
  cairo_fill(cr);

  // Menu background
  cairo_set_source_rgba(cr, surfaceR_ * 1.05f, surfaceG_ * 1.05f, surfaceB_ * 1.05f, 1.0f);
  cairo_new_path(cr);
  const float r = kMenuRadius;
  cairo_arc(cr, x_ + r, my + r, r, M_PI, 1.5 * M_PI);
  cairo_arc(cr, x_ + w_ - r, my + r, r, 1.5 * M_PI, 2.0 * M_PI);
  cairo_arc(cr, x_ + w_ - r, my + mh - r, r, 0.0, 0.5 * M_PI);
  cairo_arc(cr, x_ + r, my + mh - r, r, 0.5 * M_PI, M_PI);
  cairo_close_path(cr);
  cairo_fill(cr);

  cairo_restore(cr);

  // Menu items
  for (std::size_t i = 0; i < n; ++i) {
    const float iy = my + static_cast<float>(i) * optH_;

    // Hover highlight
    if (i == hoveredIdx_) {
      cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 0.08f);
      cairo_rectangle(cr, x_, iy, w_, optH_);
      cairo_fill(cr);
    }

    // Selected indicator
    if (i == sel_) {
      cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 0.12f);
      cairo_rectangle(cr, x_, iy, 3, optH_);
      cairo_fill(cr);
    }

    // Label
    Label lbl;
    lbl.setText(options_[i]);
    lbl.setFontSize(fontSize_ - 1);
    lbl.setColor(textR_, textG_, textB_, 1.0f);
    lbl.paintAt(cr, x_ + padding_, iy + (optH_ - 16) * 0.5f);
  }
}

} // namespace m3
