#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <cairo/cairo.h>

#include "m3/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Navigation Drawer — Modal (scrim + slide) or Standard (inline).
class NavDrawer {
public:
  struct Item {
    std::string icon;
    std::string label;
    std::function<void()> onActivate;
  };

  NavDrawer() = default;

  void setItems(std::vector<Item> items) { items_ = std::move(items); }
  void setActiveIndex(int i) { activeIdx_ = std::clamp(i, 0, static_cast<int>(items_.size()) - 1); }
  void setModal(bool m) { modal_ = m; }
  void setOpen(bool o) { open_ = o; }
  void setGeometry(float x, float y, float w, float h) { sx_ = x; sy_ = y; sw_ = w; sh_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }

  [[nodiscard]] bool open() const noexcept { return open_; }
  [[nodiscard]] bool modal() const noexcept { return modal_; }
  [[nodiscard]] float animOffset() const noexcept { return open_ ? 0.0f : -drawerW_; }

  int itemAt(float px, float py) const noexcept {
    if (!open_) return -1;
    const float dx = sx_ + animOffset();
    const float itemH = 48.0f;
    const float listY = sy_ + 24.0f;  // top padding
    if (px < dx || px > dx + drawerW_ || py < listY) return -1;
    const int idx = static_cast<int>((py - listY) / itemH);
    if (idx < 0 || idx >= static_cast<int>(items_.size())) return -1;
    if (py - listY - idx * itemH > itemH) return -1;
    return idx;
  }

  bool hitScrim(float px, float py) const {
    if (!open_ || !modal_) return false;
    const float dx = sx_ + animOffset();
    return px >= sx_ && px < sx_ + sw_ && py >= sy_ && py < sy_ + sh_
        && (px < dx || px > dx + drawerW_);
  }

  bool handlePointerDown(float, float) { return false; }

  void paint(cairo_t* cr) {
    if (!open_ || sw_ <= 0) return;

    drawerW_ = std::min(drawerW_, sw_ * 0.85f);
    drawerW_ = std::max(drawerW_, 280.0f);
    drawerH_ = sh_;

    const float dx = sx_ + animOffset();
    const float itemH = 48.0f;
    const float iconSize = 24.0f;

    // Scrim (modal only)
    if (modal_) {
      cairo_save(cr);
      cairo_set_source_rgba(cr, 0.071f, 0.047f, 0.149f, 0.32f);
      cairo_rectangle(cr, sx_, sy_, sw_, sh_);
      cairo_fill(cr);
      cairo_restore(cr);
    }

    // Drawer surface
    cairo_save(cr);
    cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 1.0f);
    cairo_rectangle(cr, dx, sy_, drawerW_, drawerH_);
    cairo_fill(cr);

    // Right edge shadow (modal)
    if (modal_) {
      cairo_set_source_rgba(cr, 0, 0, 0, 0.08f);
      cairo_rectangle(cr, dx + drawerW_ - 2, sy_, 2, drawerH_);
      cairo_fill(cr);
    }

    cairo_restore(cr);

    // Items
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
      const float iy = sy_ + 24.0f + i * itemH;
      const bool active = i == activeIdx_;

      // Active indicator
      if (active) {
        cairo_save(cr);
        const float rr = 12.0f;
        cairo_new_path(cr);
        cairo_arc(cr, dx + 8 + rr, iy + 4 + rr, rr, M_PI, 1.5 * M_PI);
        cairo_arc(cr, dx + 8 + drawerW_ - 16 - rr, iy + 4 + rr, rr, 1.5 * M_PI, 2.0 * M_PI);
        cairo_arc(cr, dx + 8 + drawerW_ - 16 - rr, iy + 4 + itemH - 8 - rr, rr, 0.0, 0.5 * M_PI);
        cairo_arc(cr, dx + 8 + rr, iy + 4 + itemH - 8 - rr, rr, 0.5 * M_PI, M_PI);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 0.16f);
        cairo_fill(cr);
        cairo_restore(cr);
      }

      // Icon
      cairo_save(cr);
      cairo_select_font_face(cr, "Symbols Nerd Font", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, iconSize);
      const float icR = active ? accentR_ : textR_;
      const float icG = active ? accentG_ : textG_;
      const float icB = active ? accentB_ : textB_;
      cairo_set_source_rgba(cr, icR, icG, icB, active ? 1.0f : 0.66f);
      cairo_move_to(cr, dx + 28, iy + itemH * 0.5f + iconSize * 0.3f);
      cairo_show_text(cr, items_[i].icon.c_str());
      cairo_restore(cr);

      // Label
      cairo_save(cr);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 14);
      cairo_set_source_rgba(cr, textR_, textG_, textB_, active ? 0.93f : 0.66f);
      cairo_move_to(cr, dx + 72, iy + itemH * 0.5f + 5);
      cairo_show_text(cr, items_[i].label.c_str());
      cairo_restore(cr);
    }
  }

private:
  std::vector<Item> items_;
  int activeIdx_ = 0;
  bool modal_ = true;
  bool open_ = false;

  float sx_ = 0, sy_ = 0, sw_ = 0, sh_ = 0;
  float drawerW_ = 360.0f, drawerH_ = 0;
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;
};

} // namespace m3
