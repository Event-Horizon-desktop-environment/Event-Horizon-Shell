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
#include "m3/controls/containers/button.hpp"
#include "m3/core/label.hpp"
#include "m3/core/primitives/state_layer.hpp"

namespace m3 {

// M3 Dialog — 3 variants: Alert, Simple, Confirmation.
// Renders scrim + dialog surface with title, content, and action buttons.
class Dialog {
public:
  enum class Variant { Alert, Simple, Confirmation };

  struct Action {
    std::string label;
    std::function<void()> callback;
  };

  Dialog() = default;

  void setVariant(Variant v) { variant_ = v; }
  void setTitle(std::string_view t) { title_.setText(t); }
  void setContent(std::string_view c) { content_.setText(c); }
  void setVisible(bool v) { visible_ = v; }
  void setGeometry(float x, float y, float w, float h) { sx_ = x; sy_ = y; sw_ = w; sh_ = h; }
  void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
  void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
  void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }
  void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }

  void addAction(std::string label, std::function<void()> cb) {
    actions_.push_back({std::move(label), std::move(cb)});
  }

  void clearActions() { actions_.clear(); }

  [[nodiscard]] bool visible() const noexcept { return visible_; }

  bool containsPoint(float px, float py) const noexcept {
    if (!visible_) return false;
    // Check scrim area
    return px >= sx_ && px < sx_ + sw_ && py >= sy_ && py < sy_ + sh_;
  }

  // Returns true if the scrim backdrop was clicked (dismiss action).
  bool handlePointerDown(float px, float py) {
    if (!visible_) return false;
    if (px < dx_ || px > dx_ + dw_ || py < dy_ || py > dy_ + dh_) {
      // Clicked outside dialog area — dismiss
      visible_ = false;
      return true;
    }
    // Check action buttons
    for (auto& btn : actionBtns_) {
      if (btn.containsPoint(px, py)) {
        btn.handlePointerDown(px, py);
        return true;
      }
    }
    return true;
  }

  bool handlePointerUp(float, float) {
    if (!visible_) return false;
    for (size_t i = 0; i < actionBtns_.size(); ++i) {
      if (actionBtns_[i].pressed()) {
        actionBtns_[i].handlePointerUp(0, 0);
        if (i < actions_.size() && actions_[i].callback) {
          actions_[i].callback();
        }
        visible_ = false;
        return true;
      }
    }
    return false;
  }

  void paint(cairo_t* cr) {
    if (!visible_ || sw_ <= 0 || sh_ <= 0) return;

    const float dialogR = 28.0f;     // corner.extra-large
    const float pad = 24.0f;
    const float actionBtnH = 40.0f;
    const float actionBtnMinW = 80.0f;
    const float actionGap = 8.0f;
    const float minDialogW = 328.0f;
    const float maxDialogW = 560.0f;

    // Layout.
    dw_ = std::clamp(sw_ * 0.85f, minDialogW, maxDialogW);
    dh_ = 0.0f; // computed below
    dx_ = sx_ + (sw_ - dw_) * 0.5f;
    dy_ = sy_ + (sh_ - dw_ * 0.6f) * 0.5f;

    // Measure title
    float titleW = 0, titleH = 0;
    title_.measureExtents(titleW, titleH);

    // Measure content
    float contentW = 0, contentH = 0;
    content_.measureExtents(contentW, contentH);

    // Height: title area + content area + action area
    const float titleAreaH = pad + titleH + 16.0f;
    const float contentAreaH = contentH > 0 ? contentH + pad : pad;
    const float actionCount = static_cast<float>(actions_.size());
    const float actionAreaH = actionBtnH + (actionCount > 0 ? pad * 0.66f : 0);
    const float totalH = titleAreaH + contentAreaH + actionAreaH + pad * 0.5f;
    dh_ = std::max(totalH, 140.0f);
    dy_ = sy_ + (sh_ - dh_) * 0.5f;

    // Action button positions (right-aligned)
    actionBtnGeoms_.resize(actions_.size());
    float btnX = dx_ + dw_ - pad;
    for (int i = static_cast<int>(actions_.size()) - 1; i >= 0; --i) {
      float bw = std::max(actionBtnMinW, 48.0f);
      float bh = actionBtnH;
      float bx = btnX - bw;
      float by = dy_ + dh_ - pad * 0.66f - actionBtnH;
      actionBtnGeoms_[i] = {bx, by, bw, bh};
      btnX = bx - actionGap;
    }

    // Scrim.
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.071f, 0.047f, 0.149f, 0.32f); // scrim @ 32%
    cairo_paint(cr);
    cairo_restore(cr);

    // Dialog surface.
    cairo_save(cr);

    // Shadow
    cairo_new_path(cr);
    cairo_arc(cr, dx_ + dialogR, dy_ + dialogR, dialogR, M_PI, 1.5 * M_PI);
    cairo_arc(cr, dx_ + dw_ - dialogR, dy_ + dialogR, dialogR, 1.5 * M_PI, 2.0 * M_PI);
    cairo_arc(cr, dx_ + dw_ - dialogR, dy_ + dh_ - dialogR, dialogR, 0.0, 0.5 * M_PI);
    cairo_arc(cr, dx_ + dialogR, dy_ + dh_ - dialogR, dialogR, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.12f);
    cairo_set_line_width(cr, 4.0f);
    cairo_stroke(cr);

    // Background (surface-container-high ≈ surface + 15% outline)
    const float bgR = blend(surfaceR_, outlineR_, 0.15f);
    const float bgG = blend(surfaceG_, outlineG_, 0.15f);
    const float bgB = blend(surfaceB_, outlineB_, 0.15f);
    cairo_new_path(cr);
    cairo_arc(cr, dx_ + dialogR, dy_ + dialogR, dialogR, M_PI, 1.5 * M_PI);
    cairo_arc(cr, dx_ + dw_ - dialogR, dy_ + dialogR, dialogR, 1.5 * M_PI, 2.0 * M_PI);
    cairo_arc(cr, dx_ + dw_ - dialogR, dy_ + dh_ - dialogR, dialogR, 0.0, 0.5 * M_PI);
    cairo_arc(cr, dx_ + dialogR, dy_ + dh_ - dialogR, dialogR, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, bgR, bgG, bgB, 1.0f);
    cairo_fill(cr);

    // Title.
    title_.setColor(textR_, textG_, textB_, 0.93f);
    title_.paintAt(cr, dx_ + pad, dy_ + pad);

    // Content.
    if (contentH > 0) {
      content_.setColor(textR_, textG_, textB_, 0.66f);
      content_.paintAt(cr, dx_ + pad, dy_ + pad + titleH + 16.0f);
    }

    // Action buttons.
    for (size_t i = 0; i < actions_.size(); ++i) {
      auto& btn = actionBtns_[i];
      auto& g = actionBtnGeoms_[i];
      btn.setGeometry(g.x, g.y, g.w, g.h);
      btn.setAccentColor(accentR_, accentG_, accentB_);
      btn.setTextColor(textR_, textG_, textB_);
      btn.paint(cr, 0);

      // Button label
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 14.0f);
      cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 1.0f);
      cairo_move_to(cr, static_cast<double>(g.x + g.w * 0.5f - 30),
                    static_cast<double>(g.y + g.h * 0.5f + 5));
      cairo_show_text(cr, actions_[i].label.c_str());
    }

    cairo_restore(cr);
  }

private:
  static float blend(float a, float b, float t) {
    return a + (b - a) * t;
  }

  struct BtnGeom { float x, y, w, h; };

  Variant variant_ = Variant::Alert;
  bool visible_ = false;

  float sx_ = 0, sy_ = 0, sw_ = 0, sh_ = 0;       // screen
  float dx_ = 0, dy_ = 0, dw_ = 0, dh_ = 0;         // dialog surface
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;

  Label title_;
  Label content_;
  std::vector<Action> actions_;
  std::vector<Button> actionBtns_;
  std::vector<BtnGeom> actionBtnGeoms_;
};

} // namespace m3
