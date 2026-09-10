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

struct HyprlandRenderM3State {
  Slider fp16SdrSlider;
  Slider useFp16Slider;
  Slider directScanoutSlider;
  Toggle cmEnabled;
  Toggle sendContentType;
  Toggle newRenderScheduling;
  Toggle expandUndersized;
  Toggle useShaderBlurBlend;

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
    apply(fp16SdrSlider); apply(useFp16Slider); apply(directScanoutSlider);
    apply(cmEnabled); apply(sendContentType); apply(newRenderScheduling);
    apply(expandUndersized); apply(useShaderBlurBlend);
    cmEnabled.setOutlineColor(outlineR_, outlineG_, outlineB_);
    sendContentType.setOutlineColor(outlineR_, outlineG_, outlineB_);
    newRenderScheduling.setOutlineColor(outlineR_, outlineG_, outlineB_);
    expandUndersized.setOutlineColor(outlineR_, outlineG_, outlineB_);
    useShaderBlurBlend.setOutlineColor(outlineR_, outlineG_, outlineB_);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(fp16SdrSlider); setH(useFp16Slider); setH(directScanoutSlider);
    setH(cmEnabled); setH(sendContentType); setH(newRenderScheduling);
    setH(expandUndersized); setH(useShaderBlurBlend);
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

    static const char* kFp16SdrTfLabels[] = {"monitor", "linear"};
    static const char* kUseFp16Labels[] = {"disable", "enable", "auto"};

    // Render
    lay.beginGroup(app, cr, glassOv, "Render", "Color, scheduling, and render hints", 5, 2,
                   textR_, textG_, textB_);
    paintToggleCard(cmEnabled, cfg.render.cm_enabled, "Color management", "Enable color management pipeline");
    paintToggleCard(sendContentType, cfg.render.send_content_type, "Send content type", "Send content type hints to compositor");
    paintToggleCard(newRenderScheduling, cfg.render.new_render_scheduling, "New render scheduling", "Use new render scheduling");
    paintToggleCard(expandUndersized, cfg.render.expand_undersized_textures, "Expand undersized textures", "Expand small textures to fill");
    paintToggleCard(useShaderBlurBlend, cfg.render.use_shader_blur_blend, "Shader blur blend", "Use experimental blurred bg blending");
    paintSliderCard(fp16SdrSlider, cfg.render.fp16_sdr_tf, 0, 1, "FP16 SDR TF", nullptr, kFp16SdrTfLabels);
    paintSliderCard(useFp16Slider, cfg.render.use_fp16, 0, 2, "Use FP16 buffer", nullptr, kUseFp16Labels);
    lay.endGroup();

    // Vulkan && Scanout
    lay.beginGroup(app, cr, glassOv, "Vulkan && Scanout", "GPU presentation behavior", 0, 1,
                   textR_, textG_, textB_);
    paintSliderCard(directScanoutSlider, cfg.render.direct_scanout, 0, 2, "Direct scanout");
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

    if (tryToggle(&cmEnabled)) return true;
    if (tryToggle(&sendContentType)) return true;
    if (tryToggle(&newRenderScheduling)) return true;
    if (tryToggle(&expandUndersized)) return true;
    if (tryToggle(&useShaderBlurBlend)) return true;

    Slider* sliders[] = {&fp16SdrSlider, &useFp16Slider, &directScanoutSlider};
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

    endToggle(&cmEnabled, &app.hyprlandConfig.render.cm_enabled);
    endToggle(&sendContentType, &app.hyprlandConfig.render.send_content_type);
    endToggle(&newRenderScheduling, &app.hyprlandConfig.render.new_render_scheduling);
    endToggle(&expandUndersized, &app.hyprlandConfig.render.expand_undersized_textures);
    endToggle(&useShaderBlurBlend, &app.hyprlandConfig.render.use_shader_blur_blend);

    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&fp16SdrSlider, &useFp16Slider, &directScanoutSlider};
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
      Slider* sliders[] = {&fp16SdrSlider, &useFp16Slider, &directScanoutSlider};
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
    reset(fp16SdrSlider); reset(useFp16Slider); reset(directScanoutSlider);
    reset(cmEnabled); reset(sendContentType); reset(newRenderScheduling);
    reset(expandUndersized); reset(useShaderBlurBlend);
  }

  void flushSliderValues(App& app) const {
    if (activeSlider_ < 0 || activeSlider_ >= 3) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&fp16SdrSlider, &useFp16Slider, &directScanoutSlider};
      return sliders[activeSlider_]->value();
    }();
    switch (activeSlider_) {
      case 0: app.hyprlandConfig.render.fp16_sdr_tf = static_cast<int>(v); break;
      case 1: app.hyprlandConfig.render.use_fp16 = static_cast<int>(v); break;
      case 2: app.hyprlandConfig.render.direct_scanout = static_cast<int>(v); break;
    }
  }
};

inline HyprlandRenderM3State& hyprlandRenderM3() {
  static HyprlandRenderM3State s;
  return s;
}

} // namespace m3::detail
