#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <cairo/cairo.h>

#include "m3/core/primitives/box.hpp"
#include "m3/core/focus_ring.hpp"
#include "m3/core/glyph.hpp"
#include "m3/core/label.hpp"
#include "m3/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Navigation Bar — 3–5 destinations, active indicator pill, icon + label.
class NavBar {
public:
  struct Destination {
    std::string label;
    std::string icon;       // glyph icon name
    std::function<void()> onActivate;
  };

  NavBar() = default;

  void setDestinations(std::vector<Destination> dests) {
    dests_ = std::move(dests);
    indicators_.resize(dests_.size());
    labels_.resize(dests_.size());
    icons_.resize(dests_.size());
  }

  void setActiveIndex(int i) { activeIdx_ = std::clamp(i, 0, static_cast<int>(dests_.size()) - 1); }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }
  void setHovered(bool h) { hovered_ = h; }
  void setPressed(bool p) { pressed_ = p; }

  [[nodiscard]] int activeIndex() const noexcept { return activeIdx_; }
  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }

  bool containsPoint(float px, float py) const noexcept {
    return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
  }

  int destAt(float px, float) const noexcept {
    if (dests_.empty()) return -1;
    const float segW = w_ / static_cast<float>(dests_.size());
    const int idx = static_cast<int>((px - x_) / segW);
    if (idx < 0 || idx >= static_cast<int>(dests_.size())) return -1;
    return idx;
  }

  bool handlePointerDown(float px, float py) {
    if (!containsPoint(px, py)) return false;
    pressed_ = true;
    return true;
  }

  bool handlePointerUp(float, float) {
    if (!pressed_) return false;
    pressed_ = false;
    return true;
  }

  void paint(cairo_t* cr) {
    if (w_ <= 0 || h_ <= 0 || dests_.empty()) return;

    const float n = static_cast<float>(dests_.size());
    const float segW = w_ / n;
    const float pillH = 32.0f;
    const float pillR = pillH * 0.5f;
    const float iconSize = 24.0f;
    const float labelH = 12.0f;

    // Background
    cairo_save(cr);
    cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 1.0f);
    cairo_rectangle(cr, x_, y_, w_, h_);
    cairo_fill(cr);
    cairo_restore(cr);

    for (int i = 0; i < static_cast<int>(dests_.size()); ++i) {
      const float segX = x_ + i * segW;
      const bool active = i == activeIdx_;

      // Active indicator pill
      if (active) {
        const float pillW = segW - 24.0f;
        const float pillX = segX + (segW - pillW) * 0.5f;
        const float pillY = y_ + 8.0f;

        cairo_save(cr);
        cairo_new_path(cr);
        cairo_arc(cr, pillX + pillR, pillY + pillR, pillR, M_PI, 1.5 * M_PI);
        cairo_arc(cr, pillX + pillW - pillR, pillY + pillR, pillR, 1.5 * M_PI, 2.0 * M_PI);
        cairo_arc(cr, pillX + pillW - pillR, pillY + pillH - pillR, pillR, 0.0, 0.5 * M_PI);
        cairo_arc(cr, pillX + pillR, pillY + pillH - pillR, pillR, 0.5 * M_PI, M_PI);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 0.16f);
        cairo_fill(cr);
        cairo_restore(cr);
      }

      // Icon
      const float iconX = segX + (segW - iconSize) * 0.5f;
      const float iconY = y_ + (active ? 6.0f : 10.0f);
      const float icR = active ? accentR_ : textR_;
      const float icG = active ? accentG_ : textG_;
      const float icB = active ? accentB_ : textB_;
      const float icA = active ? 1.0f : 0.66f;

      if (!dests_[i].icon.empty()) {
        cairo_save(cr);
        cairo_select_font_face(cr, "Symbols Nerd Font", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, iconSize);
        cairo_set_source_rgba(cr, icR, icG, icB, icA);
        cairo_move_to(cr, iconX, iconY + iconSize * 0.75f);
        cairo_show_text(cr, dests_[i].icon.c_str());
        cairo_restore(cr);
      }

      // Label (active always visible, inactive when n <= 3 or shifted)
      if (active || n <= 3.5f) {
        cairo_save(cr);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, labelH);
        cairo_set_source_rgba(cr, textR_, textG_, textB_, active ? 1.0f : 0.66f);

        cairo_text_extents_t te;
        cairo_text_extents(cr, dests_[i].label.c_str(), &te);
        const float lx = segX + (segW - te.width) * 0.5f;
        const float ly = y_ + h_ - 14.0f;
        cairo_move_to(cr, lx, ly);
        cairo_show_text(cr, dests_[i].label.c_str());
        cairo_restore(cr);
      }
    }
  }

private:
  std::vector<Destination> dests_;
  std::vector<StateLayer> indicators_;
  std::vector<Label> labels_;
  std::vector<Glyph> icons_;

  int activeIdx_ = 0;
  bool hovered_ = false, pressed_ = false;

  float x_ = 0, y_ = 0, w_ = 0, h_ = 80;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
};

} // namespace m3
