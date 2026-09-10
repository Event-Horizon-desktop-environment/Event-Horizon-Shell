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

struct HyprlandGroupM3State {
  Slider dragIntoGroupSlider;
  int activeSlider_ = -1;
  Toggle autoGroup;
  Toggle insertAfterCurrent;
  Toggle focusRemovedWindow;
  Toggle mergeGroupsOnDrag;
  Toggle mergeGroupsOnGroupbar;
  Toggle mergeFloatedIntoTiled;
  Toggle groupOnMovetoworkspace;

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
    apply(dragIntoGroupSlider);
    auto applyTg = [&](auto& w) {
      w.setAccentColor(accentR_, accentG_, accentB_);
      w.setSurfaceColor(surfaceR_, surfaceG_, surfaceB_);
      w.setTextColor(textR_, textG_, textB_);
      w.setOutlineColor(outlineR_, outlineG_, outlineB_);
    };
    applyTg(autoGroup); applyTg(insertAfterCurrent); applyTg(focusRemovedWindow);
    applyTg(mergeGroupsOnDrag); applyTg(mergeGroupsOnGroupbar);
    applyTg(mergeFloatedIntoTiled); applyTg(groupOnMovetoworkspace);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(dragIntoGroupSlider);
    setH(autoGroup); setH(insertAfterCurrent); setH(focusRemovedWindow);
    setH(mergeGroupsOnDrag); setH(mergeGroupsOnGroupbar);
    setH(mergeFloatedIntoTiled); setH(groupOnMovetoworkspace);
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

    static const char* kDragIntoGroupLabels[] = {"disabled", "enabled", "groupbar only"};

    // Group Behavior
    lay.beginGroup(app, cr, glassOv, "Group Behavior", "Window grouping behavior", 7, 1,
                   textR_, textG_, textB_);
    paintToggleCard(autoGroup, cfg.group.auto_group, "Auto group", "Automatically group spawned windows");
    paintToggleCard(insertAfterCurrent, cfg.group.insert_after_current, "Insert after current", "Insert new window after the focused one");
    paintToggleCard(focusRemovedWindow, cfg.group.focus_removed_window, "Focus removed window", "Focus the removed window when ungrouping");
    paintToggleCard(mergeGroupsOnDrag, cfg.group.merge_groups_on_drag, "Merge groups on drag", "Merge entire groups when dragging");
    paintToggleCard(mergeGroupsOnGroupbar, cfg.group.merge_groups_on_groupbar, "Merge on groupbar", "Merge groups via groupbar drag");
    paintToggleCard(mergeFloatedIntoTiled, cfg.group.merge_floated_into_tiled_on_groupbar, "Merge floated into tiled", "Allow merging floated into tiled groups");
    paintToggleCard(groupOnMovetoworkspace, cfg.group.group_on_movetoworkspace, "Group on move-to-workspace", "Group window when moving to another workspace");
    paintSliderCard(dragIntoGroupSlider, cfg.group.drag_into_group, 0, 2, "Drag into group", nullptr, kDragIntoGroupLabels);
    lay.endGroup();

    app.hyprlandContentBottom = lay.groupY + kSpacingL;
  }

  bool handlePointerDown(App&, float px, float py) {
    activeSlider_ = -1;
    auto tryToggle = [&](Toggle* tg) -> bool {
      if (tg->containsPoint(px, py)) { tg->handlePointerDown(px, py); return true; }
      return false;
    };
    if (tryToggle(&autoGroup)) return true;
    if (tryToggle(&insertAfterCurrent)) return true;
    if (tryToggle(&focusRemovedWindow)) return true;
    if (tryToggle(&mergeGroupsOnDrag)) return true;
    if (tryToggle(&mergeGroupsOnGroupbar)) return true;
    if (tryToggle(&mergeFloatedIntoTiled)) return true;
    if (tryToggle(&groupOnMovetoworkspace)) return true;
    if (dragIntoGroupSlider.containsPoint(px, py)) {
      dragIntoGroupSlider.handlePointerDown(px, py);
      activeSlider_ = 0;
      return true;
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
    endToggle(&autoGroup, &app.hyprlandConfig.group.auto_group);
    endToggle(&insertAfterCurrent, &app.hyprlandConfig.group.insert_after_current);
    endToggle(&focusRemovedWindow, &app.hyprlandConfig.group.focus_removed_window);
    endToggle(&mergeGroupsOnDrag, &app.hyprlandConfig.group.merge_groups_on_drag);
    endToggle(&mergeGroupsOnGroupbar, &app.hyprlandConfig.group.merge_groups_on_groupbar);
    endToggle(&mergeFloatedIntoTiled, &app.hyprlandConfig.group.merge_floated_into_tiled_on_groupbar);
    endToggle(&groupOnMovetoworkspace, &app.hyprlandConfig.group.group_on_movetoworkspace);
    if (activeSlider_ >= 0) {
      dragIntoGroupSlider.handlePointerUp(px, py);
      activeSlider_ = -1;
      handled = true;
    }
    return handled;
  }

  bool handlePointerMove(float px, float py) {
    if (activeSlider_ >= 0) {
      dragIntoGroupSlider.handlePointerMove(px, py);
      return true;
    }
    return false;
  }

  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(dragIntoGroupSlider);
    reset(autoGroup); reset(insertAfterCurrent); reset(focusRemovedWindow);
    reset(mergeGroupsOnDrag); reset(mergeGroupsOnGroupbar);
    reset(mergeFloatedIntoTiled); reset(groupOnMovetoworkspace);
  }

  void flushSliderValues(App& app) const {
    if (activeSlider_ < 0) return;
    app.hyprlandConfig.group.drag_into_group = static_cast<int>(dragIntoGroupSlider.value());
  }
};

inline HyprlandGroupM3State& hyprlandGroupM3() {
  static HyprlandGroupM3State s;
  return s;
}

} // namespace m3::detail
