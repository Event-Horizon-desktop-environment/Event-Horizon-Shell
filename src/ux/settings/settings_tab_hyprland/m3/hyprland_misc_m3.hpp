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

struct HyprlandMiscM3State {
  Slider vrrSlider;
  Slider forceWallpaperSlider;
  Slider renderUnfocusedFpsSlider;
  Slider initWsTokenTimeoutSlider;
  Slider initWsTrackingSlider;
  Toggle disableAutoreload;
  Toggle disableSplash;
  Toggle disableLogo;
  Toggle allowLockRestore;
  Toggle mouseMoveEnablesDpms;
  Toggle middleClickPaste;
  Toggle animateManualResizes;
  Toggle animateMouseWindowdragging;
  Toggle enableSwallow;

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
    apply(vrrSlider); apply(forceWallpaperSlider);
    apply(renderUnfocusedFpsSlider); apply(initWsTokenTimeoutSlider);
    apply(initWsTrackingSlider);
    apply(disableAutoreload); apply(disableSplash); apply(disableLogo);
    apply(allowLockRestore); apply(mouseMoveEnablesDpms);
    apply(middleClickPaste); apply(animateManualResizes);
    apply(animateMouseWindowdragging);     apply(enableSwallow);
    disableAutoreload.setOutlineColor(outlineR_, outlineG_, outlineB_);
    disableSplash.setOutlineColor(outlineR_, outlineG_, outlineB_);
    disableLogo.setOutlineColor(outlineR_, outlineG_, outlineB_);
    allowLockRestore.setOutlineColor(outlineR_, outlineG_, outlineB_);
    mouseMoveEnablesDpms.setOutlineColor(outlineR_, outlineG_, outlineB_);
    middleClickPaste.setOutlineColor(outlineR_, outlineG_, outlineB_);
    animateManualResizes.setOutlineColor(outlineR_, outlineG_, outlineB_);
    animateMouseWindowdragging.setOutlineColor(outlineR_, outlineG_, outlineB_);
    enableSwallow.setOutlineColor(outlineR_, outlineG_, outlineB_);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(vrrSlider); setH(forceWallpaperSlider);
    setH(renderUnfocusedFpsSlider); setH(initWsTokenTimeoutSlider);
    setH(initWsTrackingSlider);
    setH(disableAutoreload); setH(disableSplash); setH(disableLogo);
    setH(allowLockRestore); setH(mouseMoveEnablesDpms);
    setH(middleClickPaste); setH(animateManualResizes);
    setH(animateMouseWindowdragging);     setH(enableSwallow);
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

    static const char* kForceWallpaperLabels[] = {"default", "none", "wallpaper 1", "wallpaper 2"};
    static const char* kInitWsTrackingLabels[] = {"disable", "workspace", "monitor + workspace"};

    // General
    lay.beginGroup(app, cr, glassOv, "General", "Splash, logo, locks, and startup behavior", 7, 0,
                   textR_, textG_, textB_);
    paintToggleCard(disableAutoreload, cfg.misc.disable_autoreload, "Disable autoreload", "Prevent config auto-reload on file change");
    paintToggleCard(disableSplash, cfg.misc.disable_splash_rendering, "Disable splash rendering", "Hide the splash message on startup");
    paintToggleCard(disableLogo, cfg.misc.disable_hyprland_logo, "Disable Hyprland logo", "Hide the logo on startup");
    paintToggleCard(allowLockRestore, cfg.misc.allow_session_lock_restore, "Allow session lock restore", "Restore session after lock");
    paintToggleCard(mouseMoveEnablesDpms, cfg.misc.mouse_move_enables_dpms, "Mouse move enables DPMS", "Wake display on mouse movement");
    paintToggleCard(middleClickPaste, cfg.misc.middle_click_paste, "Middle-click paste", "Enable middle-click-paste (primary selection)");
    paintToggleCard(animateManualResizes, cfg.misc.animate_manual_resizes, "Animate manual resizes", "Animate windows when resizing manually");
    lay.endGroup();

    // Animation && Swallow
    lay.beginGroup(app, cr, glassOv, "Animation && Swallow", "Window dragging and swallow behavior", 2, 0,
                   textR_, textG_, textB_);
    paintToggleCard(animateMouseWindowdragging, cfg.misc.animate_mouse_windowdragging, "Animate mouse window dragging", "Animate windows when dragging with mouse");
    paintToggleCard(enableSwallow, cfg.misc.enable_swallow, "Enable swallow", "Enable swallow functionality for terminals");
    lay.endGroup();

    // VRR && Wallpaper && Timing
    lay.beginGroup(app, cr, glassOv, "VRR && Wallpaper && Timing", "Display refresh, wallpaper, and background fps", 0, 5,
                   textR_, textG_, textB_);
    paintSliderCard(vrrSlider, cfg.misc.vrr, 0, 2, "VRR");
    paintSliderCard(forceWallpaperSlider, cfg.misc.force_default_wallpaper, -1, 2, "Force default wallpaper", nullptr, kForceWallpaperLabels);
    paintSliderCard(initWsTrackingSlider, cfg.misc.initial_workspace_tracking, 0, 2, "Initial workspace tracking", nullptr, kInitWsTrackingLabels);
    paintSliderCard(renderUnfocusedFpsSlider, cfg.misc.render_unfocused_fps, 1, 240, "Render unfocused FPS");
    paintSliderCard(initWsTokenTimeoutSlider, cfg.misc.initial_workspace_token_timeout, 1, 120, "Workspace token timeout");
    lay.endGroup();

    app.hyprlandContentBottom = lay.groupY + kSpacingL;
  }

  bool handlePointerDown(App&, float px, float py) {
    activeSlider_ = -1;

    auto tryToggle = [&](Toggle* tg) -> bool {
      if (tg->containsPoint(px, py)) { tg->handlePointerDown(px, py); return true; }
      return false;
    };

    if (tryToggle(&disableAutoreload)) return true;
    if (tryToggle(&disableSplash)) return true;
    if (tryToggle(&disableLogo)) return true;
    if (tryToggle(&allowLockRestore)) return true;
    if (tryToggle(&mouseMoveEnablesDpms)) return true;
    if (tryToggle(&middleClickPaste)) return true;
    if (tryToggle(&animateManualResizes)) return true;
    if (tryToggle(&animateMouseWindowdragging)) return true;
    if (tryToggle(&enableSwallow)) return true;

    Slider* sliders[] = {&vrrSlider, &forceWallpaperSlider, &initWsTrackingSlider,
                         &renderUnfocusedFpsSlider, &initWsTokenTimeoutSlider};
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

    endToggle(&disableAutoreload, &app.hyprlandConfig.misc.disable_autoreload);
    endToggle(&disableSplash, &app.hyprlandConfig.misc.disable_splash_rendering);
    endToggle(&disableLogo, &app.hyprlandConfig.misc.disable_hyprland_logo);
    endToggle(&allowLockRestore, &app.hyprlandConfig.misc.allow_session_lock_restore);
    endToggle(&mouseMoveEnablesDpms, &app.hyprlandConfig.misc.mouse_move_enables_dpms);
    endToggle(&middleClickPaste, &app.hyprlandConfig.misc.middle_click_paste);
    endToggle(&animateManualResizes, &app.hyprlandConfig.misc.animate_manual_resizes);
    endToggle(&animateMouseWindowdragging, &app.hyprlandConfig.misc.animate_mouse_windowdragging);
    endToggle(&enableSwallow, &app.hyprlandConfig.misc.enable_swallow);

    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&vrrSlider, &forceWallpaperSlider, &initWsTrackingSlider,
                           &renderUnfocusedFpsSlider, &initWsTokenTimeoutSlider};
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
      Slider* sliders[] = {&vrrSlider, &forceWallpaperSlider, &initWsTrackingSlider,
                           &renderUnfocusedFpsSlider, &initWsTokenTimeoutSlider};
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
    reset(vrrSlider); reset(forceWallpaperSlider);
    reset(initWsTrackingSlider);
    reset(renderUnfocusedFpsSlider); reset(initWsTokenTimeoutSlider);
    reset(disableAutoreload); reset(disableSplash); reset(disableLogo);
    reset(allowLockRestore); reset(mouseMoveEnablesDpms);
    reset(middleClickPaste); reset(animateManualResizes);
    reset(animateMouseWindowdragging);     reset(enableSwallow);
  }

  void flushSliderValues(App& app) const {
    if (activeSlider_ < 0 || activeSlider_ >= 5) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&vrrSlider, &forceWallpaperSlider, &initWsTrackingSlider,
                                 &renderUnfocusedFpsSlider, &initWsTokenTimeoutSlider};
      return sliders[activeSlider_]->value();
    }();
    switch (activeSlider_) {
      case 0: app.hyprlandConfig.misc.vrr = static_cast<int>(v); break;
      case 1: app.hyprlandConfig.misc.force_default_wallpaper = static_cast<int>(v); break;
      case 2: app.hyprlandConfig.misc.initial_workspace_tracking = static_cast<int>(v); break;
      case 3: app.hyprlandConfig.misc.render_unfocused_fps = static_cast<int>(v); break;
      case 4: app.hyprlandConfig.misc.initial_workspace_token_timeout = static_cast<int>(v); break;
    }
  }
};

inline HyprlandMiscM3State& hyprlandMiscM3() {
  static HyprlandMiscM3State s;
  return s;
}

} // namespace m3::detail
