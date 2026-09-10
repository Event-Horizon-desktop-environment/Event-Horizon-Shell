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
#include "ux/settings/settings_tab_hyprland/m3/hyprland_m3_layout.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"

extern void draw(App& app);

namespace m3::detail {

static constexpr int kLauncherChildCount = 3;  // 0=Launcher, 1=Workspaces, 2=Overview
static constexpr int kLauncherTabW = 130;
static constexpr int kLauncherChildGap = 8;

// Child-tab bar.
inline int launcher_hit_child_tab(float px, float py, int contentX, int) {
  const int barX = contentX + 8;
  const int barY = kContentTop + 3;
  for (int i = 0; i < kLauncherChildCount; ++i) {
    const int tx = barX + 4 + i * (kLauncherTabW + kLauncherChildGap);
    if (px >= tx && px < tx + kLauncherTabW && py >= barY && py < barY + kDockChildTabH)
      return i;
  }
  return -1;
}

inline void launcher_paint_child_tab_bar(cairo_t* cr, int contentX, int contentW,
                                         float textR, float textG, float textB,
                                         float accentR, float accentG, float accentB,
                                         float surfR, float surfG, float surfB,
                                         double glassOv, int activeTab) {
  static const char* kChildLabels[] = {"Launcher", "Workspaces", "Overview"};
  const int barX = contentX + 8;
  const int barY = kContentTop;
  const int barW = contentW - 16;

  m3::Box bg;
  bg.setColor(surfR * 0.35f, surfG * 0.35f, surfB * 0.35f, static_cast<float>(0.78 * glassOv));
  bg.setRadius(10.0f);
  bg.setGeometry(static_cast<float>(barX), static_cast<float>(barY),
                 static_cast<float>(barW), static_cast<float>(kDockChildTabH + 6));
  bg.setGlassy(true);
  bg.paint(cr);

  const int tabY = barY + 3;
  const int tabH = kDockChildTabH;

  cairo_save(cr);
  cairo_rectangle(cr, static_cast<double>(barX + 5), static_cast<double>(barY),
                  static_cast<double>(barW - 10), static_cast<double>(kDockChildTabH + 6));
  cairo_clip(cr);

  for (int i = 0; i < kLauncherChildCount; ++i) {
    const int tx = barX + 4 + i * (kLauncherTabW + kLauncherChildGap);
    const bool sel = (i == activeTab);

    m3::Box tabBg;
    if (sel) {
      tabBg.setColor(0.35f * surfR + 0.20f * accentR,
                     0.35f * surfG + 0.20f * accentG,
                     0.35f * surfB + 0.20f * accentB,
                     static_cast<float>(0.78 * glassOv));
    } else {
      tabBg.setColor(surfR * 0.35f, surfG * 0.35f, surfB * 0.35f, static_cast<float>(0.78 * glassOv));
    }
    tabBg.setRadius(8.0f);
    tabBg.setGeometry(static_cast<float>(tx), static_cast<float>(tabY),
                      static_cast<float>(kLauncherTabW), static_cast<float>(tabH));
    tabBg.setGlassy(true);
    tabBg.paint(cr);

    m3::Label lbl;
    lbl.setText(kChildLabels[i]);
    lbl.setFontSize(13.0f);
    lbl.setFontWeight(sel ? 600 : 400);
    lbl.setColor(textR, textG, textB, sel ? 0.95f : 0.58f);
    float lw, lh;
    lbl.measureExtents(lw, lh);
    lbl.paintAt(cr, static_cast<float>(tx) + (static_cast<float>(kLauncherTabW) - lw) * 0.5f,
                static_cast<float>(tabY) + (static_cast<float>(tabH) - lh) * 0.5f);
  }

  cairo_restore(cr);
}

// Launcher M3 state.
struct LauncherTabM3State {
  Slider laSlider[9];
  Slider wsSlider[2];
  Slider ovSlider[5];
  Toggle wsShowApps;
  Toggle ovLiveUpdates;
  Toggle ovMultiMonitor;

  // Segmented-option chip rects from the last paint (x, y, w, h).
  float viewChipRect[2][4]{};
  float axisChipRect[2][4]{};
  float captureChipRect[2][4]{};

  int activeChild_ = 0;    // child tab owning the active slider drag
  int activeSlider_ = -1;  // slider index within activeChild_
  bool dirty_ = false;     // a chip toggled on pointer-down; commit on pointer-up

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
    for (int i = 0; i < 9; ++i) apply(laSlider[i]);
    for (int i = 0; i < 2; ++i) apply(wsSlider[i]);
    for (int i = 0; i < 5; ++i) apply(ovSlider[i]);
    apply(wsShowApps);
    wsShowApps.setOutlineColor(outlineR_, outlineG_, outlineB_);
    apply(ovLiveUpdates);
    ovLiveUpdates.setOutlineColor(outlineR_, outlineG_, outlineB_);
    apply(ovMultiMonitor);
    ovMultiMonitor.setOutlineColor(outlineR_, outlineG_, outlineB_);
  }

  // Logical pointer position (already includes the scroll offset).
  static float lx(const App& app) { return static_cast<float>(app.pointerX); }
  static float ly(const App& app) {
    return static_cast<float>(app.pointerY + settings_scroll_px(app));
  }

  void updateHover(const App& app) {
    const float px = lx(app);
    const float py = ly(app);
    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    for (int i = 0; i < 9; ++i) setH(laSlider[i]);
    for (int i = 0; i < 2; ++i) setH(wsSlider[i]);
    for (int i = 0; i < 5; ++i) setH(ovSlider[i]);
    setH(wsShowApps);
    setH(ovLiveUpdates);
    setH(ovMultiMonitor);
  }

  int sliderVal(const App& app, int child, int idx) const {
    const auto& s = app.settings;
    if (child == 0) {
      switch (idx) {
        case 0: return s.launchpadLayoutScalePct;
        case 1: return s.launchpadIconFillPct;
        case 2: return s.launchpadCellGapPx;
        case 3: return s.launchpadFolderSizePct;
        case 4: return s.launchpadFolderGapPx;
        case 5: return s.launchpadGridColumns;
        case 6: return s.launchpadGridRows;
        case 7: return s.slotPillOpacity;
        case 8: return s.launchpadDpiScalePct;
        default: return 0;
      }
    }
    if (child == 1) return (idx == 0) ? s.workspacesMaxSlots : s.workspacesMaxIcons;
    switch (idx) {
      case 0: return s.overviewCardScalePct;
      case 1: return s.overviewCardGapPx;
      case 2: return s.overviewScrollDelayMs;
      case 3: return s.overviewCloseBtnSizePx;
      default: return s.overviewSearchWidthPx;
    }
  }

  void applySliderVal(App& app, int child, int idx, int v) {
    auto& s = app.settings;
    if (child == 0) {
      switch (idx) {
        case 0: s.launchpadLayoutScalePct = v; break;
        case 1: s.launchpadIconFillPct = v; break;
        case 2: s.launchpadCellGapPx = v; break;
        case 3: s.launchpadFolderSizePct = v; break;
        case 4: s.launchpadFolderGapPx = v; break;
        case 5: s.launchpadGridColumns = v; break;
        case 6: s.launchpadGridRows = v; break;
        case 7: s.slotPillOpacity = v; s.taskbarSlotPillOpacity = v; break;
        case 8: s.launchpadDpiScalePct = v; break;
      }
      return;
    }
    if (child == 1) {
      if (idx == 0) s.workspacesMaxSlots = v;
      else s.workspacesMaxIcons = v;
      return;
    }
    switch (idx) {
      case 0: s.overviewCardScalePct = v; break;
      case 1: s.overviewCardGapPx = v; break;
      case 2: s.overviewScrollDelayMs = v; break;
      case 3: s.overviewCloseBtnSizePx = v; break;
      default: s.overviewSearchWidthPx = v; break;
    }
  }

  // Segmented option row (title + chip selector on the right).
  void paintOptionRow(cairo_t* cr, const App& app, HyprlandCardLayout& lay,
                      const char* label, int count, const char* const* labels, int selected,
                      float chipRect[][4]) {
    const int rowY = lay.rowTop(lay.row);
    ++lay.row;
    if (lay.row > 1)
      hyprland_paint_row_divider(cr, lay, rowY, textR_, textG_, textB_);

    settings_show_text(cr, lay.cardX + kCardPad, rowY + 22, label, 14.f, 500, textR_, textG_, textB_, 0.93f);

    constexpr float kChipH = 30.0f;
    constexpr float kChipGap = 8.0f;
    constexpr float kChipPadX = 14.0f;

    // Size chips from the widest label (measured at the widest weight) so no
    // label ever overflows its button.
    float maxW = 0.0f;
    for (int i = 0; i < count; ++i) {
      m3::Label meas;
      meas.setText(labels[i]);
      meas.setFontSize(12.5f);
      meas.setFontWeight(600);
      float lw, lh;
      meas.measureExtents(lw, lh);
      maxW = std::max(maxW, lw);
    }
    const float kChipW = std::max(64.0f, maxW + 2.0f * kChipPadX);
    const float totalW = count * kChipW + (count - 1) * kChipGap;
    float x = static_cast<float>(lay.cardX + lay.cardW - kCardPad) - totalW;
    const float cy = static_cast<float>(rowY) + (static_cast<float>(kHyprToggleRowH) - kChipH) * 0.5f;
    const float px = lx(app);
    const float py = ly(app);

    for (int i = 0; i < count; ++i) {
      chipRect[i][0] = x;
      chipRect[i][1] = cy;
      chipRect[i][2] = kChipW;
      chipRect[i][3] = kChipH;
      const bool sel = (i == selected);
      const bool hover = (px >= x && px < x + kChipW && py >= cy && py < cy + kChipH);

      m3::Box chip;
      if (sel) {
        chip.setColor(0.30f * surfaceR_ + 0.25f * accentR_,
                      0.30f * surfaceG_ + 0.25f * accentG_,
                      0.30f * surfaceB_ + 0.25f * accentB_,
                      hover ? 0.95f : 0.80f);
      } else {
        chip.setColor(surfaceR_ * 0.45f, surfaceG_ * 0.45f, surfaceB_ * 0.45f,
                      hover ? 0.85f : 0.70f);
      }
      chip.setRadius(9.0f);
      chip.setGeometry(x, cy, kChipW, kChipH);
      chip.setGlassy(true);
      chip.paint(cr);

      m3::Label lbl;
      lbl.setText(labels[i]);
      lbl.setFontSize(12.5f);
      lbl.setFontWeight(sel ? 600 : 400);
      lbl.setColor(sel ? 1.0f : textR_, sel ? 1.0f : textG_, sel ? 1.0f : textB_, sel ? 0.92f : 0.62f);
      float lw, lh;
      lbl.measureExtents(lw, lh);
      lbl.paintAt(cr, x + (kChipW - lw) * 0.5f, cy + (kChipH - lh) * 0.5f);

      x += kChipW + kChipGap;
    }
  }

  int hitChip(const float chipRect[][4], int count, float px, float py) const {
    for (int i = 0; i < count; ++i) {
      if (px >= chipRect[i][0] && px < chipRect[i][0] + chipRect[i][2] &&
          py >= chipRect[i][1] && py < chipRect[i][1] + chipRect[i][3])
        return i;
    }
    return -1;
  }

  void paint(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, int contentTop) {
    syncColours(app);
    applyColours();
    updateHover(app);

    HyprlandCardLayout lay;
    lay.init(contentX, contentW, contentTop);

    auto paintSliderCard = [&](Slider& sl, int child, int idx, int lo, int hi,
                               const char* label, const char* fmt,
                               const char* const* value_labels = nullptr) {
      hyprland_paint_slider_card(app, cr, lay, glassOv, sl, sliderVal(app, child, idx), lo, hi,
                                 label, textR_, textG_, textB_, fmt, value_labels);
    };

    if (app.launcherChildTab == 0) {
      lay.beginGroup(app, cr, glassOv, "Layout", "Scale and spacing for the launcher grid", 0, 5,
                     textR_, textG_, textB_);
      paintSliderCard(laSlider[0], 0, 0, 70, 150, "Layout scale", "%d %%");
      paintSliderCard(laSlider[1], 0, 1, 30, 95, "Icon size", "%d %%");
      paintSliderCard(laSlider[2], 0, 2, 0, 24, "Icon spacing", "%d px");
      paintSliderCard(laSlider[3], 0, 3, 50, 200, "Folder size", "%d %%");
      paintSliderCard(laSlider[4], 0, 4, 4, 48, "Folder spacing", "%d px");
      lay.endGroup();

      lay.beginGroup(app, cr, glassOv, "Grid", "Columns, rows, and DPI scaling", 0, 3,
                     textR_, textG_, textB_);
      paintSliderCard(laSlider[5], 0, 5, 4, 12, "Columns", "%d");
      paintSliderCard(laSlider[6], 0, 6, 3, 10, "Rows", "%d");
      paintSliderCard(laSlider[8], 0, 8, 50, 300, "DPI scale", "%d %%");
      lay.endGroup();

      lay.beginGroup(app, cr, glassOv, "Appearance", "Opacity and default view mode", 1, 1,
                     textR_, textG_, textB_);
      static const char* const kViewModeLabels[] = {"Grid", "List"};
      paintOptionRow(cr, app, lay, "View mode", 2, kViewModeLabels, app.settings.launchpadViewMode,
                     viewChipRect);
      paintSliderCard(laSlider[7], 0, 7, 0, 100, "Widget opacity", "%d %%");
      lay.endGroup();
    } else if (app.launcherChildTab == 1) {
      lay.beginGroup(app, cr, glassOv, "Workspaces", "Workspace slots and app icons", 1, 2,
                     textR_, textG_, textB_);
      hyprland_paint_toggle_card(app, cr, lay, glassOv, wsShowApps, app.settings.workspacesShowApps,
                                 "Show app icons", accentR_, accentG_, accentB_, textR_, textG_, textB_,
                                 "Show running applications as icons in the workspace strip");
      static const char* const kWsSlotLabels[17] = {"Auto", "1", "2", "3", "4", "5", "6", "7", "8",
                                                    "9", "10", "11", "12", "13", "14", "15", "16"};
      paintSliderCard(wsSlider[0], 1, 0, 0, 16, "Number of workspaces", nullptr, kWsSlotLabels);
      paintSliderCard(wsSlider[1], 1, 1, 1, 8, "Max workspace icons", "%d");
      lay.endGroup();
    } else {
      lay.beginGroup(app, cr, glassOv, "Overview", "Workspace overview layout and behavior", 3, 6,
                     textR_, textG_, textB_);
      hyprland_paint_toggle_card(app, cr, lay, glassOv, ovLiveUpdates,
                                 app.settings.overviewLiveUpdates, "Live updates", accentR_,
                                 accentG_, accentB_, textR_, textG_, textB_,
                                 "Stream live per-window frames while the overview is open");
      hyprland_paint_toggle_card(app, cr, lay, glassOv, ovMultiMonitor,
                                 app.settings.overviewMultiMonitor, "Multi-monitor", accentR_,
                                 accentG_, accentB_, textR_, textG_, textB_,
                                 "Show the overview on all monitors at once");
      static const char* const kCaptureModeLabels[] = {"Snapshot", "Screencopy"};
      paintOptionRow(cr, app, lay, "Capture mode", 2, kCaptureModeLabels,
                     app.settings.overviewCaptureMode, captureChipRect);
      static const char* const kAxisLabels[] = {"Vertical", "Horizontal"};
      paintOptionRow(cr, app, lay, "Scroll axis", 2, kAxisLabels, app.settings.overviewAxis,
                     axisChipRect);
      paintSliderCard(ovSlider[0], 2, 0, 20, 80, "Card scale", "%d %%");
      paintSliderCard(ovSlider[1], 2, 1, 8, 80, "Card spacing", "%d px");
      paintSliderCard(ovSlider[2], 2, 2, 50, 500, "Scroll settle delay", "%d ms");
      paintSliderCard(ovSlider[3], 2, 3, 20, 60, "Close button size", "%d px");
      paintSliderCard(ovSlider[4], 2, 4, 200, 800, "Search bar width", "%d px");
      lay.endGroup();
    }

    app.launcherContentBottom = lay.groupY + kSpacingL;
  }

  Slider* sliderFor(int child, int idx) {
    if (child == 0) return (idx >= 0 && idx < 9) ? &laSlider[idx] : nullptr;
    if (child == 1) return (idx >= 0 && idx < 2) ? &wsSlider[idx] : nullptr;
    return (idx >= 0 && idx < 5) ? &ovSlider[idx] : nullptr;
  }

  bool handlePointerDown(App& app, float px, float py) {
    activeSlider_ = -1;
    const int child = app.launcherChildTab;

    if (child == 0) {
      const int chip = hitChip(viewChipRect, 2, px, py);
      if (chip >= 0) {
        if (chip != app.settings.launchpadViewMode) {
          app.settings.launchpadViewMode = chip;
          dirty_ = true;
        }
        return true;
      }
      for (int i = 0; i < 9; ++i) {
        if (laSlider[i].containsPoint(px, py)) {
          laSlider[i].handlePointerDown(px, py);
          activeChild_ = 0;
          activeSlider_ = i;
          return true;
        }
      }
    } else if (child == 1) {
      if (wsShowApps.containsPoint(px, py)) {
        wsShowApps.handlePointerDown(px, py);
        return true;
      }
      for (int i = 0; i < 2; ++i) {
        if (wsSlider[i].containsPoint(px, py)) {
          wsSlider[i].handlePointerDown(px, py);
          activeChild_ = 1;
          activeSlider_ = i;
          return true;
        }
      }
    } else {
      if (ovLiveUpdates.containsPoint(px, py)) {
        ovLiveUpdates.handlePointerDown(px, py);
        return true;
      }
      if (ovMultiMonitor.containsPoint(px, py)) {
        ovMultiMonitor.handlePointerDown(px, py);
        return true;
      }
      const int capChip = hitChip(captureChipRect, 2, px, py);
      if (capChip >= 0) {
        if (capChip != app.settings.overviewCaptureMode) {
          app.settings.overviewCaptureMode = capChip;
          dirty_ = true;
        }
        return true;
      }
      const int chip = hitChip(axisChipRect, 2, px, py);
      if (chip >= 0) {
        if (chip != app.settings.overviewAxis) {
          app.settings.overviewAxis = chip;
          dirty_ = true;
        }
        return true;
      }
      for (int i = 0; i < 5; ++i) {
        if (ovSlider[i].containsPoint(px, py)) {
          ovSlider[i].handlePointerDown(px, py);
          activeChild_ = 2;
          activeSlider_ = i;
          return true;
        }
      }
    }
    return false;
  }

  bool handlePointerUp(App& app, float px, float py) {
    bool handled = false;

    if (wsShowApps.pressed()) {
      wsShowApps.handlePointerUp(px, py);
      app.settings.workspacesShowApps = !app.settings.workspacesShowApps;
      handled = true;
    }

    if (ovLiveUpdates.pressed()) {
      ovLiveUpdates.handlePointerUp(px, py);
      app.settings.overviewLiveUpdates = !app.settings.overviewLiveUpdates;
      handled = true;
    }

    if (ovMultiMonitor.pressed()) {
      ovMultiMonitor.handlePointerUp(px, py);
      app.settings.overviewMultiMonitor = !app.settings.overviewMultiMonitor;
      handled = true;
    }

    if (activeSlider_ >= 0) {
      if (Slider* s = sliderFor(activeChild_, activeSlider_); s) s->handlePointerUp(px, py);
      activeSlider_ = -1;
      handled = true;
    }

    if (dirty_) {
      dirty_ = false;
      handled = true;
    }
    return handled;
  }

  bool handlePointerMove(float px, float py) {
    if (activeSlider_ < 0) return false;
    if (Slider* s = sliderFor(activeChild_, activeSlider_); s) s->handlePointerMove(px, py);
    return true;
  }

  void handlePointerLeave() {
    activeSlider_ = -1;
    dirty_ = false;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    for (int i = 0; i < 9; ++i) reset(laSlider[i]);
    for (int i = 0; i < 2; ++i) reset(wsSlider[i]);
    for (int i = 0; i < 5; ++i) reset(ovSlider[i]);
    reset(wsShowApps);
    reset(ovLiveUpdates);
    reset(ovMultiMonitor);
  }

  void flushSliderValues(App& app) {
    if (activeSlider_ < 0) return;
    Slider* s = sliderFor(activeChild_, activeSlider_);
    if (!s) return;
    applySliderVal(app, activeChild_, activeSlider_, static_cast<int>(s->value()));
  }
};

inline LauncherTabM3State& launcherM3() {
  static LauncherTabM3State s;
  return s;
}

} // namespace m3::detail
