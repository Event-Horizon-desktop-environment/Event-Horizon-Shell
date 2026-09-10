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

struct HyprlandGeneralM3State {
  Slider borderSizeSlider;
  Slider gapsInSlider;
  Slider gapsOutSlider;
  Slider gapsWorkspacesSlider;
  Slider snapWindowGapSlider;
  Slider snapMonitorGapSlider;
  Slider extendBorderGrabAreaSlider;
  Slider resizeCornerSlider;
  Toggle allowTearing;
  Toggle snapEnabled;
  Toggle resizeOnBorder;
  Toggle snapBorderOverlap;
  Toggle snapRespectGaps;
  Toggle hoverIconOnBorder;
  Toggle noFocusFallback;
  Toggle modalParentBlocking;

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
    apply(borderSizeSlider); apply(gapsInSlider); apply(gapsOutSlider);
    apply(gapsWorkspacesSlider); apply(snapWindowGapSlider); apply(snapMonitorGapSlider);
    apply(extendBorderGrabAreaSlider); apply(resizeCornerSlider);
    apply(allowTearing); apply(snapEnabled); apply(resizeOnBorder);
    apply(snapBorderOverlap); apply(snapRespectGaps); apply(hoverIconOnBorder);
    apply(noFocusFallback); apply(modalParentBlocking);
    allowTearing.setOutlineColor(outlineR_, outlineG_, outlineB_);
    snapEnabled.setOutlineColor(outlineR_, outlineG_, outlineB_);
    resizeOnBorder.setOutlineColor(outlineR_, outlineG_, outlineB_);
    snapBorderOverlap.setOutlineColor(outlineR_, outlineG_, outlineB_);
    snapRespectGaps.setOutlineColor(outlineR_, outlineG_, outlineB_);
    hoverIconOnBorder.setOutlineColor(outlineR_, outlineG_, outlineB_);
    noFocusFallback.setOutlineColor(outlineR_, outlineG_, outlineB_);
    modalParentBlocking.setOutlineColor(outlineR_, outlineG_, outlineB_);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(borderSizeSlider); setH(gapsInSlider); setH(gapsOutSlider);
    setH(gapsWorkspacesSlider); setH(snapWindowGapSlider); setH(snapMonitorGapSlider);
    setH(extendBorderGrabAreaSlider); setH(resizeCornerSlider);
    setH(allowTearing); setH(snapEnabled); setH(resizeOnBorder);
    setH(snapBorderOverlap); setH(snapRespectGaps); setH(hoverIconOnBorder);
    setH(noFocusFallback); setH(modalParentBlocking);
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

    // Window & Gaps
    lay.beginGroup(app, cr, glassOv, "Window && Gaps", "Borders and spacing between windows", 0, 4,
                   textR_, textG_, textB_);
    paintSliderCard(borderSizeSlider, cfg.general.border_size, 0, 20, "Border size");
    paintSliderCard(gapsInSlider, cfg.general.gaps_in, 0, 50, "Inner gaps");
    paintSliderCard(gapsOutSlider, cfg.general.gaps_out, 0, 50, "Outer gaps");
    paintSliderCard(gapsWorkspacesSlider, cfg.general.gaps_workspaces, 0, 50, "Workspace gaps");
    lay.endGroup();

    // Snap
    lay.beginGroup(app, cr, glassOv, "Snap", "Window snapping behavior", 3, 2,
                   textR_, textG_, textB_);
    paintToggleCard(snapEnabled, cfg.general.snap.enabled, "Snap enabled", "Snap windows to screen edges");
    paintToggleCard(snapBorderOverlap, cfg.general.snap.border_overlap, "Border overlap", "Allow windows to overlap snap borders");
    paintToggleCard(snapRespectGaps, cfg.general.snap.respect_gaps, "Respect gaps", "Respect gap settings when snapping");
    paintSliderCard(snapWindowGapSlider, cfg.general.snap.window_gap, 0, 50, "Window gap");
    paintSliderCard(snapMonitorGapSlider, cfg.general.snap.monitor_gap, 0, 50, "Monitor gap");
    lay.endGroup();

    // Tearing
    lay.beginGroup(app, cr, glassOv, "Tearing", "Tearing behavior", 1, 0,
                   textR_, textG_, textB_);
    paintToggleCard(allowTearing, cfg.general.allow_tearing, "Allow tearing", "Allow variable refresh rate tearing");
    lay.endGroup();

    // Border Behavior
    lay.beginGroup(app, cr, glassOv, "Border Behavior", "Resize and focus behavior", 3, 2,
                   textR_, textG_, textB_);
    paintToggleCard(resizeOnBorder, cfg.general.resize_on_border, "Resize on border", "Resize window by dragging border");
    paintToggleCard(hoverIconOnBorder, cfg.general.hover_icon_on_border, "Hover icon on border", "Show cursor icon when hovering border");
    paintToggleCard(noFocusFallback, cfg.general.no_focus_fallback, "No focus fallback", "Disable focus fallback behavior");
    paintSliderCard(extendBorderGrabAreaSlider, cfg.general.extend_border_grab_area, 0, 100, "Extend border grab area");
    paintSliderCard(resizeCornerSlider, cfg.general.resize_corner, 0, 4, "Resize corner");
    lay.endGroup();

    // Layout dropdown
    {
      static const char* kHyprLayoutLabels[] = {"dwindle", "master", "scrolling", "monocle"};
      int layoutIdx = 0;
      for (int i = 0; i < 4; ++i)
        if (cfg.general.layout == kHyprLayoutLabels[i]) { layoutIdx = i; break; }
      constexpr int kComboW = 140;
      lay.beginGroup(app, cr, glassOv, "Layout", "Active window layout", 1, 0,
                     textR_, textG_, textB_);
      const int rowY = lay.cardY + kHyprSectionHeaderH;
      const int comboX = lay.cardX + lay.cardW - kCardPad - kComboW;
      const int comboY = rowY + (kHyprToggleRowH - kSettingsComboH) / 2;
      app.hyprlandLayoutComboX = comboX;
      app.hyprlandLayoutComboY = comboY;
      settings_paint_combo_closed(app, cr, comboX, comboY, kComboW, kSettingsComboH, glassOv,
                                  kHyprLayoutLabels[layoutIdx], app.hyprlandLayoutDropdownOpen,
                                  settings_scroll_px_int(app));
      lay.endGroup();
    }

    app.hyprlandContentBottom = lay.groupY + kSpacingL;
  }

  bool handlePointerDown(App&, float px, float py) {
    activeSlider_ = -1;

    auto tryToggle = [&](Toggle* tg) -> bool {
      if (tg->containsPoint(px, py)) { tg->handlePointerDown(px, py); return true; }
      return false;
    };

    if (tryToggle(&allowTearing)) return true;
    if (tryToggle(&snapEnabled)) return true;
    if (tryToggle(&resizeOnBorder)) return true;
    if (tryToggle(&snapBorderOverlap)) return true;
    if (tryToggle(&snapRespectGaps)) return true;
    if (tryToggle(&hoverIconOnBorder)) return true;
    if (tryToggle(&noFocusFallback)) return true;
    if (tryToggle(&modalParentBlocking)) return true;

    Slider* sliders[] = {&borderSizeSlider, &gapsInSlider, &gapsOutSlider, &gapsWorkspacesSlider,
                         &snapWindowGapSlider, &snapMonitorGapSlider, &extendBorderGrabAreaSlider,
                         &resizeCornerSlider};
    for (int i = 0; i < 8; ++i) {
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

    endToggle(&allowTearing, &app.hyprlandConfig.general.allow_tearing);
    endToggle(&snapEnabled, &app.hyprlandConfig.general.snap.enabled);
    endToggle(&resizeOnBorder, &app.hyprlandConfig.general.resize_on_border);
    endToggle(&snapBorderOverlap, &app.hyprlandConfig.general.snap.border_overlap);
    endToggle(&snapRespectGaps, &app.hyprlandConfig.general.snap.respect_gaps);
    endToggle(&hoverIconOnBorder, &app.hyprlandConfig.general.hover_icon_on_border);
    endToggle(&noFocusFallback, &app.hyprlandConfig.general.no_focus_fallback);
    endToggle(&modalParentBlocking, &app.hyprlandConfig.general.modal_parent_blocking);

    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&borderSizeSlider, &gapsInSlider, &gapsOutSlider, &gapsWorkspacesSlider,
                           &snapWindowGapSlider, &snapMonitorGapSlider, &extendBorderGrabAreaSlider,
                           &resizeCornerSlider};
      if (activeSlider_ < 8) {
        sliders[activeSlider_]->handlePointerUp(px, py);
      }
      activeSlider_ = -1;
      handled = true;
    }

    return handled;
  }

  bool handlePointerMove(float px, float py) {
    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&borderSizeSlider, &gapsInSlider, &gapsOutSlider, &gapsWorkspacesSlider,
                           &snapWindowGapSlider, &snapMonitorGapSlider, &extendBorderGrabAreaSlider,
                           &resizeCornerSlider};
      if (activeSlider_ < 8) {
        sliders[activeSlider_]->handlePointerMove(px, py);
      }
      return true;
    }
    return false;
  }

  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(borderSizeSlider); reset(gapsInSlider); reset(gapsOutSlider); reset(gapsWorkspacesSlider);
    reset(snapWindowGapSlider); reset(snapMonitorGapSlider); reset(extendBorderGrabAreaSlider);
    reset(resizeCornerSlider);
    reset(allowTearing); reset(snapEnabled); reset(resizeOnBorder);
    reset(snapBorderOverlap); reset(snapRespectGaps); reset(hoverIconOnBorder);
    reset(noFocusFallback); reset(modalParentBlocking);
  }

  void flushSliderValues(App& app) const {
    if (activeSlider_ < 0 || activeSlider_ >= 8) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&borderSizeSlider, &gapsInSlider, &gapsOutSlider, &gapsWorkspacesSlider,
                                 &snapWindowGapSlider, &snapMonitorGapSlider, &extendBorderGrabAreaSlider,
                                 &resizeCornerSlider};
      return sliders[activeSlider_]->value();
    }();
    switch (activeSlider_) {
      case 0: app.hyprlandConfig.general.border_size = static_cast<int>(v); break;
      case 1: app.hyprlandConfig.general.gaps_in = static_cast<int>(v); break;
      case 2: app.hyprlandConfig.general.gaps_out = static_cast<int>(v); break;
      case 3: app.hyprlandConfig.general.gaps_workspaces = static_cast<int>(v); break;
      case 4: app.hyprlandConfig.general.snap.window_gap = static_cast<int>(v); break;
      case 5: app.hyprlandConfig.general.snap.monitor_gap = static_cast<int>(v); break;
      case 6: app.hyprlandConfig.general.extend_border_grab_area = static_cast<int>(v); break;
      case 7: app.hyprlandConfig.general.resize_corner = static_cast<int>(v); break;
    }
  }
};

inline HyprlandGeneralM3State& hyprlandGeneralM3() {
  static HyprlandGeneralM3State s;
  return s;
}

} // namespace m3::detail
