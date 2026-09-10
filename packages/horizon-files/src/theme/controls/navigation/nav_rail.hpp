#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <cairo/cairo.h>

#include "theme/core/focus_ring.hpp"
#include "theme/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Navigation Rail — vertical bar, 72dp wide, stacked destinations + FAB slot.
class NavRail {
public:
  struct Destination {
    std::string label;
    std::string icon;
    std::function<void()> onActivate;
  };

  NavRail() = default;

  void setDestinations(std::vector<Destination> dests) { dests_ = std::move(dests); }
  void setActiveIndex(int i) { activeIdx_ = std::clamp(i, 0, static_cast<int>(dests_.size()) - 1); }
  void setFabVisible(bool v) { fabVisible_ = v; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }

  [[nodiscard]] int activeIndex() const noexcept { return activeIdx_; }
  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }

  bool containsPoint(float px, float py) const noexcept {
    return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
  }

  int destAt(float px, float py) const noexcept {
    if (dests_.empty() || !containsPoint(px, py)) return -1;
    const float destH = 56.0f;
    const float startY = y_ + (fabVisible_ ? 72.0f + 12.0f : 12.0f);
    const int idx = static_cast<int>((py - startY) / destH);
    if (idx < 0 || idx >= static_cast<int>(dests_.size())) return -1;
    return idx;
  }

  bool handlePointerDown(float, float) { return false; }

  void paint(cairo_t* cr) {
    if (w_ <= 0 || h_ <= 0 || dests_.empty()) return;

    const float railW = (w_ > 0) ? w_ : 72.0f;
    const float destH = 56.0f;
    const float iconSize = 24.0f;
    const float pillH = 32.0f;
    const float pillR = pillH * 0.5f;
    const float pillW = 56.0f;
    const float startY = y_ + (fabVisible_ ? 72.0f + 12.0f : 12.0f);

    // Background
    cairo_save(cr);
    cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 1.0f);
    cairo_rectangle(cr, x_, y_, railW, h_);
    cairo_fill(cr);
    cairo_restore(cr);

    // FAB slot
    if (fabVisible_) {
      const float fabD = 56.0f;
      const float fabX = x_ + (railW - fabD) * 0.5f;
      const float fabY = y_ + 12.0f;
      cairo_save(cr);
      cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 1.0f);
      cairo_arc(cr, fabX + fabD * 0.5f, fabY + fabD * 0.5f, fabD * 0.5f, 0, 2.0 * M_PI);
      cairo_fill(cr);
      cairo_set_source_rgba(cr, 1, 1, 1, 1);
      cairo_select_font_face(cr, "Symbols Nerd Font", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 24);
      cairo_move_to(cr, fabX + fabD * 0.5f - 8, fabY + fabD * 0.5f + 8);
      cairo_show_text(cr, "\uE145");  // "+" icon
      cairo_restore(cr);
    }

    // Destinations
    for (int i = 0; i < static_cast<int>(dests_.size()); ++i) {
      const float dy = startY + i * destH;
      const bool active = i == activeIdx_;

      // Active indicator
      if (active) {
        const float pillX = x_ + (railW - pillW) * 0.5f;
        const float pillY = dy + (destH - pillH) * 0.5f;
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
      const float iconX = x_ + (railW - iconSize) * 0.5f;
      const float iconY = dy + (destH - iconSize) * 0.5f - (active ? 4 : 0);
      const float icR = active ? accentR_ : textR_;
      const float icG = active ? accentG_ : textG_;
      const float icB = active ? accentB_ : textB_;
      const float icA = active ? 1.0f : 0.66f;

      cairo_save(cr);
      cairo_select_font_face(cr, "Symbols Nerd Font", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, iconSize);
      cairo_set_source_rgba(cr, icR, icG, icB, icA);
      cairo_move_to(cr, iconX, iconY + iconSize * 0.75f);
      cairo_show_text(cr, dests_[i].icon.c_str());
      cairo_restore(cr);

      // Label (active only)
      if (active) {
        cairo_save(cr);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12);
        cairo_set_source_rgba(cr, textR_, textG_, textB_, 1.0f);

        cairo_text_extents_t te;
        cairo_text_extents(cr, dests_[i].label.c_str(), &te);
        const float lx = x_ + (railW - te.width) * 0.5f;
        const float ly = dy + destH - 6.0f;
        cairo_move_to(cr, lx, ly);
        cairo_show_text(cr, dests_[i].label.c_str());
        cairo_restore(cr);
      }
    }
  }

private:
  std::vector<Destination> dests_;
  int activeIdx_ = 0;
  bool fabVisible_ = false;

  float x_ = 0, y_ = 0, w_ = 72, h_ = 0;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
};

} // namespace m3
