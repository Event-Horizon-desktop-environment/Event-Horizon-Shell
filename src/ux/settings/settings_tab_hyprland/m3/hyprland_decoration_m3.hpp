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

struct HyprlandDecorationM3State {
  Slider roundingSlider;
  Slider roundingPowerSlider;
  Slider activeOpacitySlider;
  Slider inactiveOpacitySlider;
  Slider fsOpacitySlider;
  Slider dimStrengthSlider;
  Slider dimSpecialSlider;
  Slider dimAroundSlider;
  Slider blurSizeSlider;
  Slider blurPassesSlider;
  Slider blurNoiseSlider;
  Slider blurContrastSlider;
  Slider blurBrightnessSlider;
  Slider blurVibrancySlider;
  Slider blurVibrancyDarknessSlider;
  Slider blurPopupsIgnoreAlpha;
  Slider blurInputMethodsIgnoreAlpha;
  Slider shadowRangeSlider;
  Slider shadowPowerSlider;
  Slider shadowScaleSlider;
  Slider glowRangeSlider;
  Slider glowPowerSlider;
  Slider motionBlurSamplesSlider;
  Toggle blurEnabled;
  Toggle blurXray;
  Toggle blurSpecial;
  Toggle blurNewOptimizations;
  Toggle blurIgnoreOpacity;
  Toggle blurPopups;
  Toggle blurInputMethods;
  Toggle dimInactive;
  Toggle dimModal;
  Toggle shadowEnabled;
  Toggle shadowSharp;
  Toggle borderPartOfWindow;
  Toggle glowEnabled;
  Toggle motionBlurEnabled;

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
    auto applyA = [&](auto& w) {
      w.setAccentColor(accentR_, accentG_, accentB_);
      w.setSurfaceColor(surfaceR_, surfaceG_, surfaceB_);
      w.setTextColor(textR_, textG_, textB_);
    };
    applyA(roundingSlider); applyA(roundingPowerSlider); applyA(activeOpacitySlider);
    applyA(inactiveOpacitySlider); applyA(fsOpacitySlider); applyA(dimStrengthSlider);
    applyA(dimSpecialSlider); applyA(dimAroundSlider); applyA(blurSizeSlider);
    applyA(blurPassesSlider); applyA(blurNoiseSlider); applyA(blurContrastSlider);
    applyA(blurBrightnessSlider); applyA(blurVibrancySlider); applyA(blurVibrancyDarknessSlider);
    applyA(blurPopupsIgnoreAlpha);
    applyA(blurInputMethodsIgnoreAlpha);
    applyA(shadowRangeSlider); applyA(shadowPowerSlider); applyA(shadowScaleSlider);
    applyA(glowRangeSlider); applyA(glowPowerSlider); applyA(motionBlurSamplesSlider);
    auto applyT = [&](auto& w) {
      applyA(w);
      w.setOutlineColor(outlineR_, outlineG_, outlineB_);
    };
    applyT(blurEnabled); applyT(blurXray); applyT(blurSpecial);
    applyT(blurNewOptimizations); applyT(blurIgnoreOpacity); applyT(blurPopups);
    applyT(blurInputMethods); applyT(dimInactive); applyT(dimModal);
    applyT(shadowEnabled); applyT(shadowSharp); applyT(borderPartOfWindow);
    applyT(glowEnabled); applyT(motionBlurEnabled);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(roundingSlider); setH(roundingPowerSlider); setH(activeOpacitySlider);
    setH(inactiveOpacitySlider); setH(fsOpacitySlider); setH(dimStrengthSlider);
    setH(dimSpecialSlider); setH(dimAroundSlider); setH(blurSizeSlider);
    setH(blurPassesSlider); setH(blurNoiseSlider); setH(blurContrastSlider);
    setH(blurBrightnessSlider); setH(blurVibrancySlider); setH(blurVibrancyDarknessSlider);
    setH(blurPopupsIgnoreAlpha);
    setH(blurInputMethodsIgnoreAlpha);
    setH(shadowRangeSlider); setH(shadowPowerSlider); setH(shadowScaleSlider);
    setH(glowRangeSlider); setH(glowPowerSlider); setH(motionBlurSamplesSlider);
    setH(blurEnabled); setH(blurXray); setH(blurSpecial);
    setH(blurNewOptimizations); setH(blurIgnoreOpacity); setH(blurPopups);
    setH(blurInputMethods); setH(dimInactive); setH(dimModal);
    setH(shadowEnabled); setH(shadowSharp); setH(borderPartOfWindow);
    setH(glowEnabled); setH(motionBlurEnabled);
  }

  void paint(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, int contentTop) {
    syncColours(app);
    applyColours();
    updateHover(app);

    auto& cfg = app.hyprlandConfig;
    HyprlandCardLayout lay;
    lay.init(contentX, contentW, contentTop);

    auto paintSliderCard = [&](Slider& sl, int val, int lo, int hi, const char* label, const char* fmt = nullptr) {
      hyprland_paint_slider_card(app, cr, lay, glassOv, sl, val, lo, hi, label,
                                 textR_, textG_, textB_, fmt);
    };
    auto paintToggleCard = [&](Toggle& tg, bool val, const char* label, const char* desc = nullptr) {
      hyprland_paint_toggle_card(app, cr, lay, glassOv, tg, val, label,
                                 accentR_, accentG_, accentB_, textR_, textG_, textB_, desc);
    };

    // Rounding & Opacity
    lay.beginGroup(app, cr, glassOv, "Rounding && Opacity", "Window corners and transparency", 0, 5,
                   textR_, textG_, textB_);
    paintSliderCard(roundingSlider, cfg.decoration.rounding, 0, 100, "Radius");
    paintSliderCard(roundingPowerSlider, cfg.decoration.rounding_power, 1, 10, "Rounding power");
    paintSliderCard(activeOpacitySlider, static_cast<int>(cfg.decoration.active_opacity * 100), 50, 100, "Active opacity", "%d %%");
    paintSliderCard(inactiveOpacitySlider, static_cast<int>(cfg.decoration.inactive_opacity * 100), 50, 100, "Inactive opacity", "%d %%");
    paintSliderCard(fsOpacitySlider, static_cast<int>(cfg.decoration.fullscreen_opacity * 100), 50, 100, "Fullscreen opacity", "%d %%");
    lay.endGroup();

    // Blur
    lay.beginGroup(app, cr, glassOv, "Blur", "Blur effect and optimization", 7, 3,
                   textR_, textG_, textB_);
    paintToggleCard(blurEnabled, cfg.decoration.blur.enabled, "Enabled", "Enable window blur effect");
    paintToggleCard(blurXray, cfg.decoration.blur.xray, "X-ray", "X-ray blur mode");
    paintToggleCard(blurSpecial, cfg.decoration.blur.special, "Special", "Blur special workspaces");
    paintToggleCard(blurNewOptimizations, cfg.decoration.blur.new_optimizations, "New optimizations", "Use optimized blur path");
    paintToggleCard(blurIgnoreOpacity, cfg.decoration.blur.ignore_opacity, "Ignore opacity", "Ignore window opacity for blur");
    paintToggleCard(blurPopups, cfg.decoration.blur.popups, "Blur popups", "Blur popup windows");
    paintToggleCard(blurInputMethods, cfg.decoration.blur.input_methods, "Blur input methods", "Blur input method windows");
    paintSliderCard(blurSizeSlider, cfg.decoration.blur.size, 0, 100, "Size");
    paintSliderCard(blurPassesSlider, cfg.decoration.blur.passes, 0, 10, "Passes");
    paintSliderCard(blurNoiseSlider, static_cast<int>(cfg.decoration.blur.noise * 100), 0, 100, "Noise", "%d %%");
    lay.endGroup();

    // Blur Advanced
    lay.beginGroup(app, cr, glassOv, "Blur Advanced", "Blur contrast, brightness, and color", 0, 6,
                   textR_, textG_, textB_);
    paintSliderCard(blurContrastSlider, static_cast<int>(cfg.decoration.blur.contrast * 100), 30, 200, "Contrast", "%d %%");
    paintSliderCard(blurBrightnessSlider, static_cast<int>(cfg.decoration.blur.brightness * 100), 30, 200, "Brightness", "%d %%");
    paintSliderCard(blurVibrancySlider, static_cast<int>(cfg.decoration.blur.vibrancy * 100), 0, 100, "Vibrancy", "%d %%");
    paintSliderCard(blurVibrancyDarknessSlider, static_cast<int>(cfg.decoration.blur.vibrancy_darkness * 100), 0, 100, "Vibrancy darkness", "%d %%");
    paintSliderCard(blurPopupsIgnoreAlpha, static_cast<int>(cfg.decoration.blur.popups_ignorealpha * 100), 0, 100, "Popups ignore alpha", "%d %%");
    paintSliderCard(blurInputMethodsIgnoreAlpha, static_cast<int>(cfg.decoration.blur.input_methods_ignorealpha * 100), 0, 100, "Input methods ignore alpha", "%d %%");
    lay.endGroup();

    // Dim
    lay.beginGroup(app, cr, glassOv, "Dim", "Dimming behavior for windows", 2, 3,
                   textR_, textG_, textB_);
    paintToggleCard(dimModal, cfg.decoration.dim_modal, "Dim modal", "Dim behind modal windows");
    paintToggleCard(dimInactive, cfg.decoration.dim_inactive, "Dim inactive", "Dim inactive windows");
    paintSliderCard(dimStrengthSlider, static_cast<int>(cfg.decoration.dim_strength * 100), 0, 100, "Dim strength", "%d %%");
    paintSliderCard(dimSpecialSlider, static_cast<int>(cfg.decoration.dim_special * 100), 0, 100, "Dim special", "%d %%");
    paintSliderCard(dimAroundSlider, static_cast<int>(cfg.decoration.dim_around * 100), 0, 100, "Dim around", "%d %%");
    lay.endGroup();

    // Shadow
    lay.beginGroup(app, cr, glassOv, "Shadow", "Shadow rendering settings", 2, 3,
                   textR_, textG_, textB_);
    paintToggleCard(shadowEnabled, cfg.decoration.shadow.enabled, "Enabled", "Enable window shadows");
    paintToggleCard(shadowSharp, cfg.decoration.shadow.sharp, "Sharp", "Use sharp shadow edges");
    paintSliderCard(shadowRangeSlider, cfg.decoration.shadow.range, 0, 100, "Range");
    paintSliderCard(shadowPowerSlider, cfg.decoration.shadow.render_power, 1, 10, "Render power");
    paintSliderCard(shadowScaleSlider, static_cast<int>(cfg.decoration.shadow.scale * 100), 0, 100, "Scale", "%d %%");
    lay.endGroup();

    // Glow
    lay.beginGroup(app, cr, glassOv, "Glow", "Glow rendering settings", 1, 2,
                   textR_, textG_, textB_);
    paintToggleCard(glowEnabled, cfg.decoration.glow.enabled, "Enabled", "Enable window glow effect");
    paintSliderCard(glowRangeSlider, cfg.decoration.glow.range, 0, 100, "Range");
    paintSliderCard(glowPowerSlider, cfg.decoration.glow.render_power, 1, 10, "Render power");
    lay.endGroup();

    // Motion Blur
    lay.beginGroup(app, cr, glassOv, "Motion Blur", "Motion blur settings", 1, 1,
                   textR_, textG_, textB_);
    paintToggleCard(motionBlurEnabled, cfg.decoration.motion_blur.enabled, "Enabled", "Enable motion blur for moving windows");
    paintSliderCard(motionBlurSamplesSlider, cfg.decoration.motion_blur.samples, 1, 64, "Samples");
    lay.endGroup();

    // Misc
    lay.beginGroup(app, cr, glassOv, "Misc", "Border and window geometry", 1, 0,
                   textR_, textG_, textB_);
    paintToggleCard(borderPartOfWindow, cfg.decoration.border_part_of_window, "Border part of window", "Include border in window area");
    lay.endGroup();

    app.hyprlandContentBottom = lay.groupY + kSpacingL;
  }

  bool handlePointerDown(App&, float px, float py) {
    activeSlider_ = -1;

    auto tryToggle = [&](Toggle* tg) -> bool {
      if (tg->containsPoint(px, py)) { tg->handlePointerDown(px, py); return true; }
      return false;
    };

    if (tryToggle(&blurEnabled)) return true;
    if (tryToggle(&blurXray)) return true;
    if (tryToggle(&blurSpecial)) return true;
    if (tryToggle(&blurNewOptimizations)) return true;
    if (tryToggle(&blurIgnoreOpacity)) return true;
    if (tryToggle(&blurPopups)) return true;
    if (tryToggle(&blurInputMethods)) return true;
    if (tryToggle(&dimInactive)) return true;
    if (tryToggle(&dimModal)) return true;
    if (tryToggle(&shadowEnabled)) return true;
    if (tryToggle(&shadowSharp)) return true;
    if (tryToggle(&borderPartOfWindow)) return true;
    if (tryToggle(&glowEnabled)) return true;
    if (tryToggle(&motionBlurEnabled)) return true;

    Slider* sliders[] = {&roundingSlider, &roundingPowerSlider, &activeOpacitySlider,
                         &inactiveOpacitySlider, &fsOpacitySlider,
                         &dimStrengthSlider, &dimSpecialSlider, &dimAroundSlider,
                         &blurSizeSlider, &blurPassesSlider, &blurNoiseSlider,
                         &blurContrastSlider, &blurBrightnessSlider, &blurVibrancySlider,
                         &blurVibrancyDarknessSlider, &blurPopupsIgnoreAlpha,
                         &blurInputMethodsIgnoreAlpha,
                         &shadowRangeSlider, &shadowPowerSlider, &shadowScaleSlider,
                         &glowRangeSlider, &glowPowerSlider, &motionBlurSamplesSlider};
    for (int i = 0; i < 23; ++i) {
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

    endToggle(&blurEnabled, &app.hyprlandConfig.decoration.blur.enabled);
    endToggle(&blurXray, &app.hyprlandConfig.decoration.blur.xray);
    endToggle(&blurSpecial, &app.hyprlandConfig.decoration.blur.special);
    endToggle(&blurNewOptimizations, &app.hyprlandConfig.decoration.blur.new_optimizations);
    endToggle(&blurIgnoreOpacity, &app.hyprlandConfig.decoration.blur.ignore_opacity);
    endToggle(&blurPopups, &app.hyprlandConfig.decoration.blur.popups);
    endToggle(&blurInputMethods, &app.hyprlandConfig.decoration.blur.input_methods);
    endToggle(&dimInactive, &app.hyprlandConfig.decoration.dim_inactive);
    endToggle(&dimModal, &app.hyprlandConfig.decoration.dim_modal);
    endToggle(&shadowEnabled, &app.hyprlandConfig.decoration.shadow.enabled);
    endToggle(&shadowSharp, &app.hyprlandConfig.decoration.shadow.sharp);
    endToggle(&borderPartOfWindow, &app.hyprlandConfig.decoration.border_part_of_window);
    endToggle(&glowEnabled, &app.hyprlandConfig.decoration.glow.enabled);
    endToggle(&motionBlurEnabled, &app.hyprlandConfig.decoration.motion_blur.enabled);

    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&roundingSlider, &roundingPowerSlider, &activeOpacitySlider,
                           &inactiveOpacitySlider, &fsOpacitySlider,
                           &dimStrengthSlider, &dimSpecialSlider, &dimAroundSlider,
                           &blurSizeSlider, &blurPassesSlider, &blurNoiseSlider,
                           &blurContrastSlider, &blurBrightnessSlider, &blurVibrancySlider,
                           &blurVibrancyDarknessSlider, &blurPopupsIgnoreAlpha,
                           &blurInputMethodsIgnoreAlpha,
                           &shadowRangeSlider, &shadowPowerSlider, &shadowScaleSlider,
                           &glowRangeSlider, &glowPowerSlider, &motionBlurSamplesSlider};
      if (activeSlider_ < 23) {
        sliders[activeSlider_]->handlePointerUp(px, py);
      }
      activeSlider_ = -1;
      handled = true;
    }

    return handled;
  }

  bool handlePointerMove(float px, float py) {
    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&roundingSlider, &roundingPowerSlider, &activeOpacitySlider,
                           &inactiveOpacitySlider, &fsOpacitySlider,
                           &dimStrengthSlider, &dimSpecialSlider, &dimAroundSlider,
                           &blurSizeSlider, &blurPassesSlider, &blurNoiseSlider,
                           &blurContrastSlider, &blurBrightnessSlider, &blurVibrancySlider,
                           &blurVibrancyDarknessSlider, &blurPopupsIgnoreAlpha,
                           &blurInputMethodsIgnoreAlpha,
                           &shadowRangeSlider, &shadowPowerSlider, &shadowScaleSlider,
                           &glowRangeSlider, &glowPowerSlider, &motionBlurSamplesSlider};
      if (activeSlider_ < 23) {
        sliders[activeSlider_]->handlePointerMove(px, py);
      }
      return true;
    }
    return false;
  }

  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(roundingSlider); reset(roundingPowerSlider); reset(activeOpacitySlider);
    reset(inactiveOpacitySlider); reset(fsOpacitySlider);
    reset(dimStrengthSlider); reset(dimSpecialSlider); reset(dimAroundSlider);
    reset(blurSizeSlider); reset(blurPassesSlider); reset(blurNoiseSlider);
    reset(blurContrastSlider); reset(blurBrightnessSlider); reset(blurVibrancySlider);
    reset(blurVibrancyDarknessSlider); reset(blurPopupsIgnoreAlpha);
    reset(blurInputMethodsIgnoreAlpha);
    reset(shadowRangeSlider); reset(shadowPowerSlider); reset(shadowScaleSlider);
    reset(glowRangeSlider); reset(glowPowerSlider); reset(motionBlurSamplesSlider);
    reset(blurEnabled); reset(blurXray); reset(blurSpecial);
    reset(blurNewOptimizations); reset(blurIgnoreOpacity); reset(blurPopups);
    reset(blurInputMethods); reset(dimInactive); reset(dimModal);
    reset(shadowEnabled); reset(shadowSharp); reset(borderPartOfWindow); reset(glowEnabled);
    reset(motionBlurEnabled);
  }

  void flushSliderValues(App& app) const {
    if (activeSlider_ < 0 || activeSlider_ >= 23) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&roundingSlider, &roundingPowerSlider, &activeOpacitySlider,
                                 &inactiveOpacitySlider, &fsOpacitySlider,
                                 &dimStrengthSlider, &dimSpecialSlider, &dimAroundSlider,
                                 &blurSizeSlider, &blurPassesSlider, &blurNoiseSlider,
                                 &blurContrastSlider, &blurBrightnessSlider, &blurVibrancySlider,
                                 &blurVibrancyDarknessSlider, &blurPopupsIgnoreAlpha,
                                 &blurInputMethodsIgnoreAlpha,
                                 &shadowRangeSlider, &shadowPowerSlider, &shadowScaleSlider,
                                 &glowRangeSlider, &glowPowerSlider, &motionBlurSamplesSlider};
      return sliders[activeSlider_]->value();
    }();
    switch (activeSlider_) {
      case 0: app.hyprlandConfig.decoration.rounding = static_cast<int>(v); break;
      case 1: app.hyprlandConfig.decoration.rounding_power = static_cast<int>(v); break;
      case 2: app.hyprlandConfig.decoration.active_opacity = v / 100.0; break;
      case 3: app.hyprlandConfig.decoration.inactive_opacity = v / 100.0; break;
      case 4: app.hyprlandConfig.decoration.fullscreen_opacity = v / 100.0; break;
      case 5: app.hyprlandConfig.decoration.dim_strength = v / 100.0; break;
      case 6: app.hyprlandConfig.decoration.dim_special = v / 100.0; break;
      case 7: app.hyprlandConfig.decoration.dim_around = v / 100.0; break;
      case 8: app.hyprlandConfig.decoration.blur.size = static_cast<int>(v); break;
      case 9: app.hyprlandConfig.decoration.blur.passes = static_cast<int>(v); break;
      case 10: app.hyprlandConfig.decoration.blur.noise = v / 100.0; break;
      case 11: app.hyprlandConfig.decoration.blur.contrast = v / 100.0; break;
      case 12: app.hyprlandConfig.decoration.blur.brightness = v / 100.0; break;
      case 13: app.hyprlandConfig.decoration.blur.vibrancy = v / 100.0; break;
      case 14: app.hyprlandConfig.decoration.blur.vibrancy_darkness = v / 100.0; break;
      case 15: app.hyprlandConfig.decoration.blur.popups_ignorealpha = v / 100.0; break;
      case 16: app.hyprlandConfig.decoration.blur.input_methods_ignorealpha = v / 100.0; break;
      case 17: app.hyprlandConfig.decoration.shadow.range = static_cast<int>(v); break;
      case 18: app.hyprlandConfig.decoration.shadow.render_power = static_cast<int>(v); break;
      case 19: app.hyprlandConfig.decoration.shadow.scale = v / 100.0; break;
      case 20: app.hyprlandConfig.decoration.glow.range = static_cast<int>(v); break;
      case 21: app.hyprlandConfig.decoration.glow.render_power = static_cast<int>(v); break;
      case 22: app.hyprlandConfig.decoration.motion_blur.samples = static_cast<int>(v); break;
    }
  }
};

inline HyprlandDecorationM3State& hyprlandDecorationM3() {
  static HyprlandDecorationM3State s;
  return s;
}

} // namespace m3::detail
