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

struct HyprlandScrollingM3State {
  Slider colWidthSlider;
  Slider focusFitSlider;
  Slider followMinVisibleSlider;
  Slider directionSlider;
  Toggle fullscreenOneCol;
  Toggle followFocus;
  Toggle wrapFocus;
  Toggle wrapSwapcol;

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
    apply(colWidthSlider); apply(focusFitSlider); apply(followMinVisibleSlider); apply(directionSlider);
    apply(fullscreenOneCol); apply(followFocus); apply(wrapFocus); apply(wrapSwapcol);
    fullscreenOneCol.setOutlineColor(outlineR_, outlineG_, outlineB_);
    followFocus.setOutlineColor(outlineR_, outlineG_, outlineB_);
    wrapFocus.setOutlineColor(outlineR_, outlineG_, outlineB_);
    wrapSwapcol.setOutlineColor(outlineR_, outlineG_, outlineB_);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(colWidthSlider); setH(focusFitSlider); setH(followMinVisibleSlider); setH(directionSlider);
    setH(fullscreenOneCol); setH(followFocus); setH(wrapFocus); setH(wrapSwapcol);
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

    // Scrolling Layout
    lay.beginGroup(app, cr, glassOv, "Scrolling Layout", "Column layout and scrolling", 4, 4,
                   textR_, textG_, textB_);
    paintToggleCard(fullscreenOneCol, cfg.scrolling.fullscreen_on_one_column, "Fullscreen on one column", "Expand to full width on single column");
    paintToggleCard(followFocus, cfg.scrolling.follow_focus, "Follow focus", "Scroll to follow focused window");
    paintToggleCard(wrapFocus, cfg.scrolling.wrap_focus, "Wrap focus", "Wrap around when moving focus");
    paintToggleCard(wrapSwapcol, cfg.scrolling.wrap_swapcol, "Wrap swap column", "Wrap around when swapping columns");
    paintSliderCard(colWidthSlider, static_cast<int>(cfg.scrolling.column_width * 100), 20, 100, "Column width", "%d %%");
    paintSliderCard(focusFitSlider, cfg.scrolling.focus_fit_method, 0, 2, "Focus fit method");
    paintSliderCard(followMinVisibleSlider, static_cast<int>(cfg.scrolling.follow_min_visible * 100), 0, 100, "Follow min visible", "%d %%");
    static const char* kDirectionLabels[] = {"left", "right", "down", "up"};
    int directionIdx = 0;
    for (int i = 0; i < 4; ++i)
      if (cfg.scrolling.direction == kDirectionLabels[i]) { directionIdx = i; break; }
    paintSliderCard(directionSlider, directionIdx, 0, 3, "Direction", nullptr, kDirectionLabels);
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

    if (tryToggle(&fullscreenOneCol)) return true;
    if (tryToggle(&followFocus)) return true;
    if (tryToggle(&wrapFocus)) return true;
    if (tryToggle(&wrapSwapcol)) return true;

    Slider* sliders[] = {&colWidthSlider, &focusFitSlider, &followMinVisibleSlider, &directionSlider};
    for (int i = 0; i < 4; ++i) {
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

    endToggle(&fullscreenOneCol, &app.hyprlandConfig.scrolling.fullscreen_on_one_column);
    endToggle(&followFocus, &app.hyprlandConfig.scrolling.follow_focus);
    endToggle(&wrapFocus, &app.hyprlandConfig.scrolling.wrap_focus);
    endToggle(&wrapSwapcol, &app.hyprlandConfig.scrolling.wrap_swapcol);

    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&colWidthSlider, &focusFitSlider, &followMinVisibleSlider, &directionSlider};
      if (activeSlider_ < 4) {
        sliders[activeSlider_]->handlePointerUp(px, py);
      }
      activeSlider_ = -1;
      handled = true;
    }

    return handled;
  }

  bool handlePointerMove(float px, float py) {
    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&colWidthSlider, &focusFitSlider, &followMinVisibleSlider, &directionSlider};
      if (activeSlider_ < 4) {
        sliders[activeSlider_]->handlePointerMove(px, py);
      }
      return true;
    }
    return false;
  }

  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(colWidthSlider); reset(focusFitSlider); reset(followMinVisibleSlider); reset(directionSlider);
    reset(fullscreenOneCol); reset(followFocus); reset(wrapFocus); reset(wrapSwapcol);
  }

  void flushSliderValues(App& app) const {
    if (activeSlider_ < 0 || activeSlider_ >= 4) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&colWidthSlider, &focusFitSlider, &followMinVisibleSlider, &directionSlider};
      return sliders[activeSlider_]->value();
    }();
    switch (activeSlider_) {
      case 0: app.hyprlandConfig.scrolling.column_width = v / 100.0; break;
      case 1: app.hyprlandConfig.scrolling.focus_fit_method = static_cast<int>(v); break;
      case 2: app.hyprlandConfig.scrolling.follow_min_visible = v / 100.0; break;
      case 3: {
        static const char* kDirectionLabels[] = {"left", "right", "down", "up"};
        app.hyprlandConfig.scrolling.direction = kDirectionLabels[static_cast<int>(v)];
        break;
      }
    }
  }
};

inline HyprlandScrollingM3State& hyprlandScrollingM3() {
  static HyprlandScrollingM3State s;
  return s;
}

} // namespace m3::detail
