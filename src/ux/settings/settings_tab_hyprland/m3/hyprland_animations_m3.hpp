#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <cairo/cairo.h>

#include "m3/controls/input/toggle.hpp"
#include "m3/core/label.hpp"
#include "m3/core/primitives/box.hpp"

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/data/hyprland/settings_hyprland_data.hpp"
#include "ux/settings/settings_tab_hyprland/common/hyprland_anim_common.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_m3_layout.hpp"

extern void draw(App& app);
extern void hyprland_commit_cfg(App& app);

namespace m3::detail {

struct HyprlandAnimationsM3State {
  int activeSlider_ = -1;
  Toggle masterToggle;
  Toggle workspaceWraparound;
  Toggle animToggles_[kAnimEntryCount];

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
      w.setOutlineColor(outlineR_, outlineG_, outlineB_);
    };
    apply(masterToggle); apply(workspaceWraparound);
    for (int i = 0; i < kAnimEntryCount; ++i)
      apply(animToggles_[i]);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(masterToggle); setH(workspaceWraparound);
    for (int i = 0; i < kAnimEntryCount; ++i)
      setH(animToggles_[i]);
  }

  void paint(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, int contentTop) {
    syncColours(app);
    applyColours();
    updateHover(app);

    auto& cfg = app.hyprlandConfig;
    HyprlandCardLayout lay;
    lay.init(contentX, contentW, contentTop);

    auto paintToggleCard = [&](Toggle& tg, bool val, const char* label, const char* desc = nullptr) {
      hyprland_paint_toggle_card(app, cr, lay, glassOv, tg, val, label,
                                 accentR_, accentG_, accentB_, textR_, textG_, textB_, desc);
    };

    // Animation Master Toggle
    lay.beginGroup(app, cr, glassOv, "Animation Master Toggle", "Global animation toggle for all window effects", 2, 0,
                   textR_, textG_, textB_);
    paintToggleCard(masterToggle, cfg.animations.enabled, "Enable animations",
                    "Global animation toggle for all window effects");
    paintToggleCard(workspaceWraparound, cfg.animations.workspace_wraparound, "Workspace wraparound",
                    "Slide between first and last workspaces");
    lay.endGroup();

    static const char* kAnimSubs[kAnimCategoryCount] = {
      "Global animation entry",
      "Window and layer surface animations",
      "Fade effects across surfaces",
      "Workspace switch animations",
      "Border, zoom, and monitor animations",
    };

    for (int cat = 0; cat < kAnimCategoryCount; ++cat) {
      auto& catMeta = kAnimCategories[cat];
      lay.beginGroup(app, cr, glassOv, catMeta.label, kAnimSubs[cat], catMeta.entry_count, 0,
                     textR_, textG_, textB_);

      for (int ei = catMeta.entry_start; ei < catMeta.entry_start + catMeta.entry_count; ++ei) {
        auto& meta = kAnimEntries[ei];

        auto it = std::find_if(cfg.animations.entries.begin(), cfg.animations.entries.end(),
          [&](auto& e) { return e.name == meta.name; });
        bool en = (it != cfg.animations.entries.end()) ? it->enabled : true;
        double speed = (it != cfg.animations.entries.end()) ? it->speed : 3.0;
        std::string curve = (it != cfg.animations.entries.end()) ? it->curve : "default";
        std::string style = (it != cfg.animations.entries.end()) ? it->style : "";
        bool isInherited = (it == cfg.animations.entries.end());

        char buf[128];
        if (isInherited) {
          std::snprintf(buf, sizeof(buf), "Inherited (%.1f, default)", 3.0);
        } else {
          std::snprintf(buf, sizeof(buf), "%.1f, %s", speed, curve.c_str());
          if (!style.empty()) {
            std::string tmp = std::string(buf) + ", " + style;
            std::snprintf(buf, sizeof(buf), "%s", tmp.c_str());
          }
        }

        hyprland_paint_anim_card(app, cr, lay, glassOv, animToggles_[ei], en, meta.label, buf,
                                 en ? (isInherited ? 0.40f : 0.50f) : 0.20f,
                                 accentR_, accentG_, accentB_, textR_, textG_, textB_);
      }
      lay.endGroup();
    }

    app.hyprlandContentBottom = lay.groupY + kSpacingL;
  }

  // 3-dot hit test.
  int hitDot(float px, float py, int contentX, int contentW, int ct) const {
    HyprlandCardLayout lay;
    lay.init(contentX, contentW, ct);

    // Skip the "Animation Master Toggle" group (label + two toggle rows)
    lay.skipSection(2, 0);

    for (int cat = 0; cat < kAnimCategoryCount; ++cat) {
      auto& catMeta = kAnimCategories[cat];
      lay.skipSection(catMeta.entry_count, 0);
      for (int ei = catMeta.entry_start; ei < catMeta.entry_start + catMeta.entry_count; ++ei) {
        const int rowY = lay.cardY + kHyprSectionHeaderH + (ei - catMeta.entry_start) * kHyprToggleRowH;
        const int dotX = lay.cardX + lay.cardW - kHyprAnimDotRight;
        const int dotCY = rowY + kHyprToggleRowH / 2;
        if (px >= dotX - kHyprAnimDotHitW && px < dotX + kHyprAnimDotHitW &&
            py >= dotCY - kHyprAnimDotHitH && py < dotCY + kHyprAnimDotHitH)
          return ei;
      }
    }
    return -1;
  }

  // Input: pointer down.
  bool handlePointerDown(App&, float px, float py) {
    // Master toggle
    if (masterToggle.containsPoint(px, py)) {
      masterToggle.handlePointerDown(px, py);
      return true;
    }
    if (workspaceWraparound.containsPoint(px, py)) {
      workspaceWraparound.handlePointerDown(px, py);
      return true;
    }

    // Per-animation toggles
    for (int i = 0; i < kAnimEntryCount; ++i) {
      if (animToggles_[i].containsPoint(px, py)) {
        animToggles_[i].handlePointerDown(px, py);
        return true;
      }
    }

    return false;
  }

  // Input: pointer up.
  bool handlePointerUp(App& app, float px, float py) {
    bool handled = false;

    if (masterToggle.pressed()) {
      masterToggle.handlePointerUp(px, py);
      app.hyprlandConfig.animations.enabled = !app.hyprlandConfig.animations.enabled;
      handled = true;
    }

    if (workspaceWraparound.pressed()) {
      workspaceWraparound.handlePointerUp(px, py);
      app.hyprlandConfig.animations.workspace_wraparound = !app.hyprlandConfig.animations.workspace_wraparound;
      handled = true;
    }

    for (int i = 0; i < kAnimEntryCount; ++i) {
      if (animToggles_[i].pressed()) {
        animToggles_[i].handlePointerUp(px, py);
        auto& entry = find_or_create_entry(app, kAnimEntries[i].name);
        entry.enabled = !entry.enabled;
        handled = true;
      }
    }

    return handled;
  }

  bool handlePointerMove(float, float) {
    return false;
  }

  void handlePointerLeave() {
    masterToggle.handlePointerLeave();
    workspaceWraparound.handlePointerLeave();
    for (int i = 0; i < kAnimEntryCount; ++i)
      animToggles_[i].handlePointerLeave();
  }

  void flushSliderValues(App&) const {}
};

inline HyprlandAnimationsM3State& hyprlandAnimationsM3() {
  static HyprlandAnimationsM3State s;
  return s;
}

} // namespace m3::detail
