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

struct HyprlandCursorM3State {
  Slider noHWCursorsSlider;
  Slider noBreakFsVrrSlider;
  Slider useCpuBufferSlider;
  int activeSlider_ = -1;
  Toggle enableHyprcursor;
  Toggle syncGsettings;
  Toggle zoomRigid;
  Toggle zoomDetachedCamera;
  Toggle hideOnKeyPress;
  Toggle hideOnTouch;
  Toggle hideOnTablet;
  Toggle warpBack;
  Toggle zoomDisableAA;
  Toggle ecoNoUpdateNews;
  Toggle ecoNoDonationNag;
  Toggle ecoEnforcePermissions;

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
    apply(noHWCursorsSlider); apply(noBreakFsVrrSlider); apply(useCpuBufferSlider);
    auto applyTg = [&](auto& w) {
      w.setAccentColor(accentR_, accentG_, accentB_);
      w.setSurfaceColor(surfaceR_, surfaceG_, surfaceB_);
      w.setTextColor(textR_, textG_, textB_);
      w.setOutlineColor(outlineR_, outlineG_, outlineB_);
    };
    applyTg(enableHyprcursor); applyTg(syncGsettings);
    applyTg(zoomRigid);
    applyTg(zoomDetachedCamera); applyTg(hideOnKeyPress); applyTg(hideOnTouch);
    applyTg(hideOnTablet); applyTg(warpBack); applyTg(zoomDisableAA);
    applyTg(ecoNoUpdateNews); applyTg(ecoNoDonationNag); applyTg(ecoEnforcePermissions);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(noHWCursorsSlider); setH(noBreakFsVrrSlider); setH(useCpuBufferSlider);
    setH(enableHyprcursor); setH(syncGsettings);
    setH(zoomRigid);
    setH(zoomDetachedCamera); setH(hideOnKeyPress); setH(hideOnTouch);
    setH(hideOnTablet); setH(warpBack); setH(zoomDisableAA);
    setH(ecoNoUpdateNews); setH(ecoNoDonationNag); setH(ecoEnforcePermissions);
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

    static const char* kHwCursorLabels[] = {"disabled", "enabled", "auto"};
    static const char* kNoBreakFsVrrLabels[] = {"disable", "enable", "auto"};
    static const char* kUseCpuBufferLabels[] = {"disable", "enable", "auto"};

    // Cursor
    lay.beginGroup(app, cr, glassOv, "Cursor", "Cursor rendering and visibility", 5, 1,
                   textR_, textG_, textB_);
    paintToggleCard(enableHyprcursor, cfg.cursor.enable_hyprcursor, "Enable hyprcursor", "Use hyprcursor theme format");
    paintToggleCard(syncGsettings, cfg.cursor.sync_gsettings_theme, "Sync gsettings theme", "Sync cursor theme from gsettings");
    paintToggleCard(hideOnKeyPress, cfg.cursor.hide_on_key_press, "Hide on key press", "Hide cursor when any key is pressed");
    paintToggleCard(hideOnTouch, cfg.cursor.hide_on_touch, "Hide on touch", "Hide cursor on touch input");
    paintToggleCard(hideOnTablet, cfg.cursor.hide_on_tablet, "Hide on tablet", "Hide cursor on tablet input");
    paintSliderCard(noHWCursorsSlider, cfg.cursor.no_hardware_cursors, 0, 2, "Hardware cursors", nullptr, kHwCursorLabels);
    lay.endGroup();

    // Zoom && Behavior
    lay.beginGroup(app, cr, glassOv, "Zoom && Behavior", "Cursor zoom and keyboard behavior", 4, 2,
                   textR_, textG_, textB_);
    paintToggleCard(zoomRigid, cfg.cursor.zoom_rigid, "Zoom rigid", "Keep zoom center fixed");
    paintToggleCard(zoomDetachedCamera, cfg.cursor.zoom_detached_camera, "Zoom detached camera", "Detached zoom camera");
    paintToggleCard(zoomDisableAA, cfg.cursor.zoom_disable_aa, "Zoom disable AA", "Disable antialiasing while zoomed");
    paintToggleCard(warpBack, cfg.cursor.warp_back_after_non_mouse_input, "Warp back after non-mouse", "Return cursor after keyboard navigation");
    paintSliderCard(noBreakFsVrrSlider, cfg.cursor.no_break_fs_vrr, 0, 2, "No break FS VRR", nullptr, kNoBreakFsVrrLabels);
    paintSliderCard(useCpuBufferSlider, cfg.cursor.use_cpu_buffer, 0, 2, "Use CPU buffer", nullptr, kUseCpuBufferLabels);
    lay.endGroup();

    // Ecosystem
    lay.beginGroup(app, cr, glassOv, "Ecosystem", "Notifications and permission checks", 3, 0,
                   textR_, textG_, textB_);
    paintToggleCard(ecoNoUpdateNews, cfg.cursor.ecosystem.no_update_news, "No update news", "Suppress Hyprland update notifications");
    paintToggleCard(ecoNoDonationNag, cfg.cursor.ecosystem.no_donation_nag, "No donation nag", "Suppress donation prompts");
    paintToggleCard(ecoEnforcePermissions, cfg.cursor.ecosystem.enforce_permissions, "Enforce permissions", "Enforce permission checks");
    lay.endGroup();

    app.hyprlandContentBottom = lay.groupY + kSpacingL;
  }

  bool handlePointerDown(App&, float px, float py) {
    activeSlider_ = -1;
    auto tryToggle = [&](Toggle* tg) -> bool {
      if (tg->containsPoint(px, py)) { tg->handlePointerDown(px, py); return true; }
      return false;
    };

    if (tryToggle(&enableHyprcursor)) return true;
    if (tryToggle(&syncGsettings)) return true;
    if (tryToggle(&hideOnKeyPress)) return true;
    if (tryToggle(&hideOnTouch)) return true;
    if (tryToggle(&hideOnTablet)) return true;
    if (tryToggle(&zoomRigid)) return true;
    if (tryToggle(&zoomDetachedCamera)) return true;
    if (tryToggle(&zoomDisableAA)) return true;
    if (tryToggle(&warpBack)) return true;
    if (tryToggle(&ecoNoUpdateNews)) return true;
    if (tryToggle(&ecoNoDonationNag)) return true;
    if (tryToggle(&ecoEnforcePermissions)) return true;

    Slider* sliders[] = {&noHWCursorsSlider, &noBreakFsVrrSlider, &useCpuBufferSlider};
    for (int i = 0; i < 3; ++i) {
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

    endToggle(&enableHyprcursor, &app.hyprlandConfig.cursor.enable_hyprcursor);
    endToggle(&syncGsettings, &app.hyprlandConfig.cursor.sync_gsettings_theme);
    endToggle(&hideOnKeyPress, &app.hyprlandConfig.cursor.hide_on_key_press);
    endToggle(&hideOnTouch, &app.hyprlandConfig.cursor.hide_on_touch);
    endToggle(&hideOnTablet, &app.hyprlandConfig.cursor.hide_on_tablet);
    endToggle(&zoomRigid, &app.hyprlandConfig.cursor.zoom_rigid);
    endToggle(&zoomDetachedCamera, &app.hyprlandConfig.cursor.zoom_detached_camera);
    endToggle(&zoomDisableAA, &app.hyprlandConfig.cursor.zoom_disable_aa);
    endToggle(&warpBack, &app.hyprlandConfig.cursor.warp_back_after_non_mouse_input);
    endToggle(&ecoNoUpdateNews, &app.hyprlandConfig.cursor.ecosystem.no_update_news);
    endToggle(&ecoNoDonationNag, &app.hyprlandConfig.cursor.ecosystem.no_donation_nag);
    endToggle(&ecoEnforcePermissions, &app.hyprlandConfig.cursor.ecosystem.enforce_permissions);

    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&noHWCursorsSlider, &noBreakFsVrrSlider, &useCpuBufferSlider};
      if (activeSlider_ < 3) {
        sliders[activeSlider_]->handlePointerUp(px, py);
      }
      activeSlider_ = -1;
      handled = true;
    }

    return handled;
  }

  bool handlePointerMove(float px, float py) {
    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&noHWCursorsSlider, &noBreakFsVrrSlider, &useCpuBufferSlider};
      if (activeSlider_ < 3) {
        sliders[activeSlider_]->handlePointerMove(px, py);
      }
      return true;
    }
    return false;
  }

  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(noHWCursorsSlider); reset(noBreakFsVrrSlider); reset(useCpuBufferSlider);
    reset(enableHyprcursor); reset(syncGsettings);
    reset(zoomRigid);
    reset(zoomDetachedCamera); reset(hideOnKeyPress); reset(hideOnTouch);
    reset(hideOnTablet); reset(warpBack); reset(zoomDisableAA);
    reset(ecoNoUpdateNews); reset(ecoNoDonationNag); reset(ecoEnforcePermissions);
  }

  void flushSliderValues(App& app) const {
    if (activeSlider_ < 0 || activeSlider_ >= 3) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&noHWCursorsSlider, &noBreakFsVrrSlider, &useCpuBufferSlider};
      return sliders[activeSlider_]->value();
    }();
    switch (activeSlider_) {
      case 0: app.hyprlandConfig.cursor.no_hardware_cursors = static_cast<int>(v); break;
      case 1: app.hyprlandConfig.cursor.no_break_fs_vrr = static_cast<int>(v); break;
      case 2: app.hyprlandConfig.cursor.use_cpu_buffer = static_cast<int>(v); break;
    }
  }
};

inline HyprlandCursorM3State& hyprlandCursorM3() {
  static HyprlandCursorM3State s;
  return s;
}

} // namespace m3::detail
