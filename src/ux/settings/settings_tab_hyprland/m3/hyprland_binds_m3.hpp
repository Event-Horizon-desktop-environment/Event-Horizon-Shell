#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <cairo/cairo.h>

#include "m3/controls/input/toggle.hpp"
#include "m3/controls/input/slider.hpp"
#include "m3/core/label.hpp"
#include "m3/core/primitives/box.hpp"

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/data/hyprland/settings_hyprland_data.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_m3_layout.hpp"

extern void draw(App& app);
extern void hyprland_commit_cfg(App& app);

namespace m3::detail {

struct HyprlandBindsM3State {
  Slider scrollDelaySlider;
  Slider focusMethodSlider;
  Toggle backAndForth;
  Toggle allowCycles;

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
    apply(scrollDelaySlider); apply(focusMethodSlider);
    apply(backAndForth); apply(allowCycles);
    backAndForth.setOutlineColor(outlineR_, outlineG_, outlineB_);
    allowCycles.setOutlineColor(outlineR_, outlineG_, outlineB_);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(scrollDelaySlider); setH(focusMethodSlider);
    setH(backAndForth); setH(allowCycles);
  }

  void paint(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, int contentTop) {
    syncColours(app);
    applyColours();
    updateHover(app);

    auto& cfg = app.hyprlandConfig;
    HyprlandCardLayout lay;
    lay.init(contentX, contentW, contentTop);

    auto paintSliderCard = [&](Slider& sl, int val, int lo, int hi, const char* label) {
      hyprland_paint_slider_card(app, cr, lay, glassOv, sl, val, lo, hi, label,
                                 textR_, textG_, textB_);
    };
    auto paintToggleCard = [&](Toggle& tg, bool val, const char* label, const char* desc = nullptr) {
      hyprland_paint_toggle_card(app, cr, lay, glassOv, tg, val, label,
                                 accentR_, accentG_, accentB_, textR_, textG_, textB_, desc);
    };

    // Bind Behavior
    lay.beginGroup(app, cr, glassOv, "Bind Behavior", "Workspace and keybind behavior", 2, 2,
                   textR_, textG_, textB_);
    paintToggleCard(backAndForth, cfg.binds.workspace_back_and_forth, "Workspace back and forth", "Toggle back to previous workspace");
    paintToggleCard(allowCycles, cfg.binds.allow_workspace_cycles, "Allow workspace cycles", "Allow cycling past end workspaces");
    paintSliderCard(scrollDelaySlider, cfg.binds.scroll_event_delay, 0, 300, "Scroll event delay");
    paintSliderCard(focusMethodSlider, cfg.binds.focus_preferred_method, 0, 2, "Focus preferred method");
    lay.endGroup();

    app.hyprlandContentBottom = lay.groupY + kSpacingL;
  }

  bool handlePointerDown(App&, float px, float py) {
    activeSlider_ = -1;

    auto tryToggle = [&](Toggle* tg) -> bool {
      if (tg->containsPoint(px, py)) {
        tg->handlePointerDown(px, py);
        return true;
      }
      return false;
    };

    if (tryToggle(&backAndForth)) return true;
    if (tryToggle(&allowCycles)) return true;

    Slider* sliders[] = {&scrollDelaySlider, &focusMethodSlider};
    for (int i = 0; i < 2; ++i) {
      if (sliders[i]->containsPoint(px, py)) {
        sliders[i]->handlePointerDown(px, py);
        activeSlider_ = i;
        return true;
      }
    }

    return false;
  }

  bool handlePointerUp(App& app, float px, float py) {
    bool handled = false;

    auto endToggle = [&](Toggle* tg, bool* setting) {
      if (!tg->pressed()) return;
      tg->handlePointerUp(px, py);
      *setting = !*setting;
      handled = true;
    };

    endToggle(&backAndForth, &app.hyprlandConfig.binds.workspace_back_and_forth);
    endToggle(&allowCycles, &app.hyprlandConfig.binds.allow_workspace_cycles);

    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&scrollDelaySlider, &focusMethodSlider};
      if (activeSlider_ < 2) {
        sliders[activeSlider_]->handlePointerUp(px, py);
      }
      activeSlider_ = -1;
      handled = true;
    }

    return handled;
  }

  bool handlePointerMove(float px, float py) {
    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&scrollDelaySlider, &focusMethodSlider};
      if (activeSlider_ < 2) {
        sliders[activeSlider_]->handlePointerMove(px, py);
      }
      return true;
    }
    return false;
  }

  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(scrollDelaySlider); reset(focusMethodSlider);
    reset(backAndForth); reset(allowCycles);
  }

  void flushSliderValues(App& app) const {
    if (activeSlider_ < 0 || activeSlider_ >= 2) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&scrollDelaySlider, &focusMethodSlider};
      return sliders[activeSlider_]->value();
    }();
    switch (activeSlider_) {
      case 0: app.hyprlandConfig.binds.scroll_event_delay = static_cast<int>(v); break;
      case 1: app.hyprlandConfig.binds.focus_preferred_method = static_cast<int>(v); break;
    }
  }
};

inline HyprlandBindsM3State& hyprlandBindsM3() {
  static HyprlandBindsM3State s;
  return s;
}

} // namespace m3::detail
