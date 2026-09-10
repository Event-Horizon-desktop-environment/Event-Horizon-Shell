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

// M3 Tabs — Primary (full-width, indicator bar) or Secondary (compact).
class Tabs {
public:
  struct Tab {
    std::string label;
    std::string icon;  // optional, primary only
    std::function<void()> onActivate;
  };

  enum class Variant { Primary, Secondary };

  Tabs() = default;

  void setTabs(std::vector<Tab> tabs) { tabs_ = std::move(tabs); }
  void setActiveIndex(int i) { activeIdx_ = std::clamp(i, 0, static_cast<int>(tabs_.size()) - 1); }
  void setVariant(Variant v) { variant_ = v; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }

  [[nodiscard]] int activeIndex() const noexcept { return activeIdx_; }

  bool containsPoint(float px, float py) const noexcept {
    return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
  }

  int tabAt(float px) const noexcept {
    if (tabs_.empty()) return -1;
    const float segW = w_ / static_cast<float>(tabs_.size());
    const int idx = static_cast<int>((px - x_) / segW);
    if (idx < 0 || idx >= static_cast<int>(tabs_.size())) return -1;
    return idx;
  }

  bool handlePointerDown(float, float) { return false; }

  void paint(cairo_t* cr) {
    if (w_ <= 0 || h_ <= 0 || tabs_.empty()) return;

    const float n = static_cast<float>(tabs_.size());
    const float segW = w_ / n;
    const bool primary = variant_ == Variant::Primary;
    const float fontSize = primary ? 14.0f : 12.0f;
    const float iconSize = 20.0f;
    const float barH = 3.0f;

    // Background
    cairo_save(cr);
    cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 1.0f);
    cairo_rectangle(cr, x_, y_, w_, h_);
    cairo_fill(cr);
    cairo_restore(cr);

    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
      const float segX = x_ + i * segW;
      const bool active = i == activeIdx_;

      // Active indicator bar
      if (active) {
        const float barW = segW * (primary ? 1.0f : 0.5f);
        const float barX = segX + (segW - barW) * 0.5f;
        const float barY = y_ + h_ - barH;
        cairo_save(cr);
        cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 1.0f);
        cairo_rectangle(cr, barX, barY, barW, barH);
        cairo_fill(cr);
        cairo_restore(cr);
      }

      // Icon (primary only)
      if (primary && !tabs_[i].icon.empty()) {
        cairo_save(cr);
        cairo_select_font_face(cr, "Symbols Nerd Font", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, iconSize);
        cairo_set_source_rgba(cr, textR_, textG_, textB_, active ? 0.93f : 0.66f);
        cairo_move_to(cr, segX + (segW - iconSize) * 0.5f, y_ + h_ * 0.3f + iconSize * 0.3f);
        cairo_show_text(cr, tabs_[i].icon.c_str());
        cairo_restore(cr);
      }

      // Label
      cairo_save(cr);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                             primary ? CAIRO_FONT_WEIGHT_NORMAL : CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, fontSize);
      cairo_set_source_rgba(cr, textR_, textG_, textB_, active ? 0.93f : 0.66f);

      cairo_text_extents_t te;
      cairo_text_extents(cr, tabs_[i].label.c_str(), &te);
      const float lx = segX + (segW - te.width) * 0.5f;
      const float ly = primary ? y_ + h_ * 0.72f + fontSize * 0.35f
                               : y_ + h_ * 0.5f + fontSize * 0.35f;
      cairo_move_to(cr, lx, ly);
      cairo_show_text(cr, tabs_[i].label.c_str());
      cairo_restore(cr);
    }
  }

private:
  std::vector<Tab> tabs_;
  int activeIdx_ = 0;
  Variant variant_ = Variant::Primary;

  float x_ = 0, y_ = 0, w_ = 0, h_ = 48;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
};

} // namespace m3
