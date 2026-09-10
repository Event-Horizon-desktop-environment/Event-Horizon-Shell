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

struct HyprlandDwindleM3State {
  Slider forceSplitSlider;
  Slider specScaleSlider;
  Slider splitWidthSlider;
  Slider defaultSplitRatioSlider;
  Slider splitBiasSlider;
  Toggle preserveSplit;
  Toggle smartSplit;
  Toggle smartResizing;
  Toggle useActiveForSplits;
  Toggle permanentDirectionOverride;
  Toggle preciseMouseMove;

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
    apply(forceSplitSlider); apply(specScaleSlider); apply(splitWidthSlider);
    apply(defaultSplitRatioSlider); apply(splitBiasSlider);
    apply(preserveSplit); apply(smartSplit); apply(smartResizing);
    apply(useActiveForSplits); apply(permanentDirectionOverride); apply(preciseMouseMove);
    preserveSplit.setOutlineColor(outlineR_, outlineG_, outlineB_);
    smartSplit.setOutlineColor(outlineR_, outlineG_, outlineB_);
    smartResizing.setOutlineColor(outlineR_, outlineG_, outlineB_);
    useActiveForSplits.setOutlineColor(outlineR_, outlineG_, outlineB_);
    permanentDirectionOverride.setOutlineColor(outlineR_, outlineG_, outlineB_);
    preciseMouseMove.setOutlineColor(outlineR_, outlineG_, outlineB_);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(forceSplitSlider); setH(specScaleSlider); setH(splitWidthSlider);
    setH(defaultSplitRatioSlider); setH(splitBiasSlider);
    setH(preserveSplit); setH(smartSplit); setH(smartResizing);
    setH(useActiveForSplits); setH(permanentDirectionOverride); setH(preciseMouseMove);
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

    static const char* kForceSplitLabels[] = {"follow mouse", "left", "right"};
    static const char* kSplitBiasLabels[] = {"directional", "current"};

    // Split Behavior
    lay.beginGroup(app, cr, glassOv, "Split Behavior", "Dwindle splitting and resizing", 6, 5,
                   textR_, textG_, textB_);
    paintToggleCard(preserveSplit, cfg.dwindle.preserve_split, "Preserve split", "Keep split direction when reopening");
    paintToggleCard(smartSplit, cfg.dwindle.smart_split, "Smart split", "Split in direction with more space");
    paintToggleCard(smartResizing, cfg.dwindle.smart_resizing, "Smart resizing", "Resize neighbor when splitting");
    paintToggleCard(useActiveForSplits, cfg.dwindle.use_active_for_splits, "Use active for splits", "Use active window for split direction");
    paintToggleCard(permanentDirectionOverride, cfg.dwindle.permanent_direction_override, "Persistent direction", "Keep preselect direction until changed");
    paintToggleCard(preciseMouseMove, cfg.dwindle.precise_mouse_move, "Precise mouse move", "Drop moved windows at precise cursor point");
    paintSliderCard(forceSplitSlider, cfg.dwindle.force_split, 0, 2, "Force split", nullptr, kForceSplitLabels);
    paintSliderCard(specScaleSlider, static_cast<int>(cfg.dwindle.special_scale_factor * 100), 10, 100, "Special scale factor", "%d %%");
    paintSliderCard(splitWidthSlider, static_cast<int>(cfg.dwindle.split_width_multiplier * 100), 10, 300, "Split width multiplier", "%d %%");
    paintSliderCard(defaultSplitRatioSlider, static_cast<int>(cfg.dwindle.default_split_ratio * 100), 10, 190, "Default split ratio", "%d %%");
    paintSliderCard(splitBiasSlider, cfg.dwindle.split_bias, 0, 1, "Split bias", nullptr, kSplitBiasLabels);
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

    if (tryToggle(&preserveSplit)) return true;
    if (tryToggle(&smartSplit)) return true;
    if (tryToggle(&smartResizing)) return true;
    if (tryToggle(&useActiveForSplits)) return true;
    if (tryToggle(&permanentDirectionOverride)) return true;
    if (tryToggle(&preciseMouseMove)) return true;

    Slider* sliders[] = {&forceSplitSlider, &specScaleSlider, &splitWidthSlider, &defaultSplitRatioSlider, &splitBiasSlider};
    for (int i = 0; i < 5; ++i) {
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

    endToggle(&preserveSplit, &app.hyprlandConfig.dwindle.preserve_split);
    endToggle(&smartSplit, &app.hyprlandConfig.dwindle.smart_split);
    endToggle(&smartResizing, &app.hyprlandConfig.dwindle.smart_resizing);
    endToggle(&useActiveForSplits, &app.hyprlandConfig.dwindle.use_active_for_splits);
    endToggle(&permanentDirectionOverride, &app.hyprlandConfig.dwindle.permanent_direction_override);
    endToggle(&preciseMouseMove, &app.hyprlandConfig.dwindle.precise_mouse_move);

    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&forceSplitSlider, &specScaleSlider, &splitWidthSlider, &defaultSplitRatioSlider, &splitBiasSlider};
      if (activeSlider_ < 5) {
        sliders[activeSlider_]->handlePointerUp(px, py);
      }
      activeSlider_ = -1;
      handled = true;
    }

    return handled;
  }

  bool handlePointerMove(float px, float py) {
    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&forceSplitSlider, &specScaleSlider, &splitWidthSlider, &defaultSplitRatioSlider, &splitBiasSlider};
      if (activeSlider_ < 5) {
        sliders[activeSlider_]->handlePointerMove(px, py);
      }
      return true;
    }
    return false;
  }

  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(forceSplitSlider); reset(specScaleSlider); reset(splitWidthSlider);
    reset(defaultSplitRatioSlider); reset(splitBiasSlider);
    reset(preserveSplit); reset(smartSplit); reset(smartResizing);
    reset(useActiveForSplits); reset(permanentDirectionOverride); reset(preciseMouseMove);
  }

  void flushSliderValues(App& app) const {
    if (activeSlider_ < 0 || activeSlider_ >= 5) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&forceSplitSlider, &specScaleSlider, &splitWidthSlider, &defaultSplitRatioSlider, &splitBiasSlider};
      return sliders[activeSlider_]->value();
    }();
    switch (activeSlider_) {
      case 0: app.hyprlandConfig.dwindle.force_split = static_cast<int>(v); break;
      case 1: app.hyprlandConfig.dwindle.special_scale_factor = v / 100.0; break;
      case 2: app.hyprlandConfig.dwindle.split_width_multiplier = v / 100.0; break;
      case 3: app.hyprlandConfig.dwindle.default_split_ratio = v / 100.0; break;
      case 4: app.hyprlandConfig.dwindle.split_bias = static_cast<int>(v); break;
    }
  }
};

inline HyprlandDwindleM3State& hyprlandDwindleM3() {
  static HyprlandDwindleM3State s;
  return s;
}

} // namespace m3::detail
