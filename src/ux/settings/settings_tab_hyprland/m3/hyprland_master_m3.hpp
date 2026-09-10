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

struct HyprlandMasterM3State {
  Slider mfactSlider;
  Slider specialScaleSlider;
  Slider slaveCountSlider;
  Slider newStatusSlider;
  Slider newOnActiveSlider;
  Slider orientationSlider;
  Slider centerFallbackSlider;
  Toggle newOnTop;
  Toggle allowSmallSplit;
  Toggle smartResizing;
  Toggle dropAtCursor;
  Toggle alwaysKeepPosition;
  Toggle focusMasterOnClose;

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
    apply(mfactSlider); apply(specialScaleSlider); apply(slaveCountSlider);
    apply(newStatusSlider); apply(newOnActiveSlider); apply(orientationSlider);
    apply(centerFallbackSlider);
    apply(newOnTop); apply(allowSmallSplit); apply(smartResizing);
    apply(dropAtCursor); apply(alwaysKeepPosition); apply(focusMasterOnClose);
    newOnTop.setOutlineColor(outlineR_, outlineG_, outlineB_);
    allowSmallSplit.setOutlineColor(outlineR_, outlineG_, outlineB_);
    smartResizing.setOutlineColor(outlineR_, outlineG_, outlineB_);
    dropAtCursor.setOutlineColor(outlineR_, outlineG_, outlineB_);
    alwaysKeepPosition.setOutlineColor(outlineR_, outlineG_, outlineB_);
    focusMasterOnClose.setOutlineColor(outlineR_, outlineG_, outlineB_);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(mfactSlider); setH(specialScaleSlider); setH(slaveCountSlider);
    setH(newStatusSlider); setH(newOnActiveSlider); setH(orientationSlider);
    setH(centerFallbackSlider);
    setH(newOnTop); setH(allowSmallSplit); setH(smartResizing);
    setH(dropAtCursor); setH(alwaysKeepPosition); setH(focusMasterOnClose);
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

    static const char* kNewStatusLabels[] = {"master", "slave", "inherit"};
    static const char* kNewOnActiveLabels[] = {"before", "after", "none"};
    static const char* kOrientationLabels[] = {"left", "right", "top", "bottom", "center"};
    static const char* kCenterFallbackLabels[] = {"left", "right", "top", "bottom"};

    int newStatusIdx = 0, newOnActiveIdx = 0, orientationIdx = 0, centerFallbackIdx = 0;
    for (int i = 0; i < 3; ++i)
      if (cfg.master.new_status == kNewStatusLabels[i]) { newStatusIdx = i; break; }
    for (int i = 0; i < 3; ++i)
      if (cfg.master.new_on_active == kNewOnActiveLabels[i]) { newOnActiveIdx = i; break; }
    for (int i = 0; i < 5; ++i)
      if (cfg.master.orientation == kOrientationLabels[i]) { orientationIdx = i; break; }
    for (int i = 0; i < 4; ++i)
      if (cfg.master.center_master_fallback == kCenterFallbackLabels[i]) { centerFallbackIdx = i; break; }

    // Master Layout
    lay.beginGroup(app, cr, glassOv, "Master Layout", "Master window placement and sizing", 6, 7,
                   textR_, textG_, textB_);
    paintToggleCard(newOnTop, cfg.master.new_on_top, "New on top", "Place new windows at top of stack");
    paintToggleCard(allowSmallSplit, cfg.master.allow_small_split, "Allow small split", "Allow splitting with small windows");
    paintToggleCard(smartResizing, cfg.master.smart_resizing, "Smart resizing", "Resize direction from cursor position");
    paintToggleCard(dropAtCursor, cfg.master.drop_at_cursor, "Drop at cursor", "Drop dragged windows at cursor position");
    paintToggleCard(alwaysKeepPosition, cfg.master.always_keep_position, "Always keep position", "Keep master position with no slaves");
    paintToggleCard(focusMasterOnClose, cfg.master.focus_master_on_close, "Focus master on close", "Focus master window when slave is closed");
    paintSliderCard(mfactSlider, static_cast<int>(cfg.master.mfact * 100), 20, 80, "M-Factor", "%d %%");
    paintSliderCard(specialScaleSlider, static_cast<int>(cfg.master.special_scale_factor * 100), 10, 100, "Special scale factor", "%d %%");
    paintSliderCard(slaveCountSlider, cfg.master.slave_count_for_center_master, 1, 10, "Slave count for center master");
    paintSliderCard(newStatusSlider, newStatusIdx, 0, 2, "New window status", nullptr, kNewStatusLabels);
    paintSliderCard(newOnActiveSlider, newOnActiveIdx, 0, 2, "New window placement", nullptr, kNewOnActiveLabels);
    paintSliderCard(orientationSlider, orientationIdx, 0, 4, "Orientation", nullptr, kOrientationLabels);
    paintSliderCard(centerFallbackSlider, centerFallbackIdx, 0, 3, "Center master fallback", nullptr, kCenterFallbackLabels);
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

    if (tryToggle(&newOnTop)) return true;
    if (tryToggle(&allowSmallSplit)) return true;
    if (tryToggle(&smartResizing)) return true;
    if (tryToggle(&dropAtCursor)) return true;
    if (tryToggle(&alwaysKeepPosition)) return true;
    if (tryToggle(&focusMasterOnClose)) return true;

    Slider* sliders[] = {&mfactSlider, &specialScaleSlider, &slaveCountSlider,
                         &newStatusSlider, &newOnActiveSlider, &orientationSlider, &centerFallbackSlider};
    for (int i = 0; i < 7; ++i) {
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

    endToggle(&newOnTop, &app.hyprlandConfig.master.new_on_top);
    endToggle(&allowSmallSplit, &app.hyprlandConfig.master.allow_small_split);
    endToggle(&smartResizing, &app.hyprlandConfig.master.smart_resizing);
    endToggle(&dropAtCursor, &app.hyprlandConfig.master.drop_at_cursor);
    endToggle(&alwaysKeepPosition, &app.hyprlandConfig.master.always_keep_position);
    endToggle(&focusMasterOnClose, &app.hyprlandConfig.master.focus_master_on_close);

    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&mfactSlider, &specialScaleSlider, &slaveCountSlider,
                           &newStatusSlider, &newOnActiveSlider, &orientationSlider, &centerFallbackSlider};
      if (activeSlider_ < 7) {
        sliders[activeSlider_]->handlePointerUp(px, py);
      }
      activeSlider_ = -1;
      handled = true;
    }

    return handled;
  }

  bool handlePointerMove(float px, float py) {
    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&mfactSlider, &specialScaleSlider, &slaveCountSlider,
                           &newStatusSlider, &newOnActiveSlider, &orientationSlider, &centerFallbackSlider};
      if (activeSlider_ < 7) {
        sliders[activeSlider_]->handlePointerMove(px, py);
      }
      return true;
    }
    return false;
  }

  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(mfactSlider); reset(specialScaleSlider); reset(slaveCountSlider);
    reset(newStatusSlider); reset(newOnActiveSlider); reset(orientationSlider);
    reset(centerFallbackSlider);
    reset(newOnTop); reset(allowSmallSplit); reset(smartResizing);
    reset(dropAtCursor); reset(alwaysKeepPosition); reset(focusMasterOnClose);
  }

  void flushSliderValues(App& app) const {
    if (activeSlider_ < 0 || activeSlider_ >= 7) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&mfactSlider, &specialScaleSlider, &slaveCountSlider,
                                 &newStatusSlider, &newOnActiveSlider, &orientationSlider, &centerFallbackSlider};
      return sliders[activeSlider_]->value();
    }();
    static const char* kNewStatusLabels[] = {"master", "slave", "inherit"};
    static const char* kNewOnActiveLabels[] = {"before", "after", "none"};
    static const char* kOrientationLabels[] = {"left", "right", "top", "bottom", "center"};
    static const char* kCenterFallbackLabels[] = {"left", "right", "top", "bottom"};
    switch (activeSlider_) {
      case 0: app.hyprlandConfig.master.mfact = v / 100.0; break;
      case 1: app.hyprlandConfig.master.special_scale_factor = v / 100.0; break;
      case 2: app.hyprlandConfig.master.slave_count_for_center_master = static_cast<int>(v); break;
      case 3: app.hyprlandConfig.master.new_status = kNewStatusLabels[static_cast<int>(v)]; break;
      case 4: app.hyprlandConfig.master.new_on_active = kNewOnActiveLabels[static_cast<int>(v)]; break;
      case 5: app.hyprlandConfig.master.orientation = kOrientationLabels[static_cast<int>(v)]; break;
      case 6: app.hyprlandConfig.master.center_master_fallback = kCenterFallbackLabels[static_cast<int>(v)]; break;
    }
  }
};

inline HyprlandMasterM3State& hyprlandMasterM3() {
  static HyprlandMasterM3State s;
  return s;
}

} // namespace m3::detail
