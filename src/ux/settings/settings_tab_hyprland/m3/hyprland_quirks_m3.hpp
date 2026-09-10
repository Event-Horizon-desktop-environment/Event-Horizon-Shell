#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <cairo/cairo.h>

#include "m3/controls/input/slider.hpp"
#include "m3/controls/input/toggle.hpp"
#include "m3/core/label.hpp"
#include "m3/core/primitives/box.hpp"

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/data/hyprland/settings_hyprland_data.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_m3_layout.hpp"

extern void draw(App& app);
extern void hyprland_commit_cfg(App& app);

namespace m3::detail {

struct HyprlandQuirksM3State {
  Slider preferHdrSlider;
  Toggle skipNonKms;
  int activeSlider_ = -1;

  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;

  void syncColours(const App& app) {
    float a_r = accentR_, a_g = accentG_, a_b = accentB_;
    float t_r = textR_, t_g = textG_, t_b = textB_;
    float s_r = surfaceR_, s_g = surfaceG_, s_b = surfaceB_;
    float o_r = outlineR_, o_g = outlineG_, o_b = outlineB_;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    accentR_ = a_r; accentG_ = a_g; accentB_ = a_b;
    textR_ = t_r; textG_ = t_g; textB_ = t_b;
    surfaceR_ = s_r; surfaceG_ = s_g; surfaceB_ = s_b;
    outlineR_ = o_r; outlineG_ = o_g; outlineB_ = o_b;
  }

  void applyColours() {
    auto apply = [&](auto& w) {
      w.setAccentColor(accentR_, accentG_, accentB_);
      w.setSurfaceColor(surfaceR_, surfaceG_, surfaceB_);
      w.setTextColor(textR_, textG_, textB_);
    };
    apply(preferHdrSlider);
    apply(skipNonKms);
    skipNonKms.setOutlineColor(outlineR_, outlineG_, outlineB_);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;
    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(preferHdrSlider); setH(skipNonKms);
  }

  void paint(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, int contentTop) {
    syncColours(app);
    applyColours();
    updateHover(app);

    auto& cfg = app.hyprlandConfig;
    HyprlandCardLayout lay;
    lay.init(contentX, contentW, contentTop);

    auto paintSliderCard = [&](Slider& sl, int val, int lo, int hi, const char* label,
                               const char* fmt = nullptr, const char* const* value_labels = nullptr) {
      hyprland_paint_slider_card(app, cr, lay, glassOv, sl, val, lo, hi, label,
                                 textR_, textG_, textB_, fmt, value_labels);
    };
    auto paintToggleCard = [&](Toggle& tg, bool val, const char* label, const char* desc = nullptr) {
      hyprland_paint_toggle_card(app, cr, lay, glassOv, tg, val, label,
                                 accentR_, accentG_, accentB_, textR_, textG_, textB_, desc);
    };

    static const char* kPreferHdrLabels[] = {"disable", "prefer hdr", "always hdr"};

    // Quirks
    lay.beginGroup(app, cr, glassOv, "Quirks", "Compatibility and output quirks", 1, 1,
                   textR_, textG_, textB_);
    paintToggleCard(skipNonKms, cfg.quirks.skip_non_kms_dmabuf_formats, "Skip non-KMS DMABUF", "Skip non-KMS compatible DMABUF formats");
    paintSliderCard(preferHdrSlider, cfg.quirks.prefer_hdr, 0, 2, "Prefer HDR", nullptr, kPreferHdrLabels);
    lay.endGroup();

    app.hyprlandContentBottom = lay.groupY + kSpacingL;
  }

  bool handlePointerDown(App&, float px, float py) {
    activeSlider_ = -1;
    if (skipNonKms.containsPoint(px, py)) {
      skipNonKms.handlePointerDown(px, py);
      return true;
    }
    if (preferHdrSlider.containsPoint(px, py)) {
      preferHdrSlider.handlePointerDown(px, py);
      activeSlider_ = 0;
      return true;
    }
    return false;
  }

  bool handlePointerUp(App& app, float px, float py) {
    bool handled = false;
    if (skipNonKms.pressed()) {
      skipNonKms.handlePointerUp(px, py);
      app.hyprlandConfig.quirks.skip_non_kms_dmabuf_formats = !app.hyprlandConfig.quirks.skip_non_kms_dmabuf_formats;
      handled = true;
    }
    if (activeSlider_ >= 0) {
      preferHdrSlider.handlePointerUp(px, py);
      activeSlider_ = -1;
      handled = true;
    }
    return handled;
  }

  bool handlePointerMove(float px, float py) {
    if (activeSlider_ >= 0) {
      preferHdrSlider.handlePointerMove(px, py);
      return true;
    }
    return false;
  }

  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(preferHdrSlider); reset(skipNonKms);
  }

  void flushSliderValues(App& app) const {
    if (activeSlider_ < 0) return;
    app.hyprlandConfig.quirks.prefer_hdr = static_cast<int>(preferHdrSlider.value());
  }
};

inline HyprlandQuirksM3State& hyprlandQuirksM3() {
  static HyprlandQuirksM3State s;
  return s;
}

} // namespace m3::detail
