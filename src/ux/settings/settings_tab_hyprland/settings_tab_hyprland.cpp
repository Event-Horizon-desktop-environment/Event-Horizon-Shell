#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "m3/controls/input/toggle.hpp"
#include "m3/core/primitives/box.hpp"
#include "m3/core/label.hpp"

#include "ux/settings/data/hyprland/settings_hyprland_data.hpp"
#include "ux/settings/settings_tab_hyprland/settings_tab_hyprland.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/utils/events/settings_event_handlers.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"

extern void draw(App& app);

using HyprCfg = eh::settings_hyprland::HyprlandConfig;

void hyprland_commit_cfg(App& app) {
  eh::settings_hyprland::write_all(app.hyprlandConfig);
  eh::settings_hyprland::apply_config();
}

// M3 sub-tab includes.
#include "ux/settings/settings_tab_hyprland/m3/hyprland_general_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_decoration_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_input_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_binds_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_dwindle_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_master_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_scrolling_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_animations_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_misc_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_render_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_cursor_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_group_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_xwayland_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_opengl_m3.hpp"
#include "ux/settings/settings_tab_hyprland/m3/hyprland_quirks_m3.hpp"

// Child-tab labels.
static const char* kHyprChildLabels[] = {
  "General", "Decoration", "Input", "Binds", "Dwindle",
  "Master", "Scrolling", "Animations", "Misc", "Render", "Cursor",
  "Group", "XWayland", "OpenGL", "Quirks"
};
static constexpr int kHyprChildCount = 15;
static constexpr int kTabW = 108;
static constexpr int kChildTabGap = 8;

// Child-tab bar.
static void paint_hyprland_child_tab_bar(cairo_t* cr, int contentX, int contentW,
                                          float textR, float textG, float textB,
                                          float accentR, float accentG, float accentB,
                                          float surfR, float surfG, float surfB,
                                          double glassOv,
                                          int activeTab, int tabScrollPx) {
  const int barX = contentX + 8;
  const int barY = kContentTop;
  const int barW = contentW - 16;

  m3::Box bg;
  bg.setColor(surfR * 0.35f, surfG * 0.35f, surfB * 0.35f, static_cast<float>(0.78 * glassOv));
  bg.setRadius(10.0f);
  bg.setGeometry(static_cast<float>(barX), static_cast<float>(barY),
                 static_cast<float>(barW), static_cast<float>(kDockChildTabH + 8));
  bg.setGlassy(true);
  bg.paint(cr);

  const int tabY = barY + 4;
  const int tabH = kDockChildTabH;
  const int tabsW = kHyprChildCount * kTabW + (kHyprChildCount - 1) * kChildTabGap;
  int startX = barX + 5;
  if (tabsW < barW - 10) {
    startX += (barW - 10 - tabsW) / 2;
  } else {
    startX -= tabScrollPx;
  }

  cairo_save(cr);
  cairo_rectangle(cr, static_cast<double>(barX + 5), static_cast<double>(barY),
                  static_cast<double>(barW - 10), static_cast<double>(kDockChildTabH + 8));
  cairo_clip(cr);

  for (int i = 0; i < kHyprChildCount; ++i) {
    const int tx = startX + i * (kTabW + kChildTabGap);
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
                      static_cast<float>(kTabW), static_cast<float>(tabH));
    tabBg.setGlassy(true);
    tabBg.paint(cr);

    m3::Label lbl;
    lbl.setText(kHyprChildLabels[i]);
    lbl.setFontSize(13.0f);
    lbl.setFontWeight(sel ? 600 : 400);
    lbl.setColor(textR, textG, textB, sel ? 0.95f : 0.58f);
    float lw, lh;
    lbl.measureExtents(lw, lh);
    lbl.paintAt(cr, static_cast<float>(tx) + (static_cast<float>(kTabW) - lw) * 0.5f,
                static_cast<float>(tabY) + (static_cast<float>(tabH) - lh) * 0.5f);
  }

  cairo_restore(cr);
}

int hyprland_hit_child_tab(float px, float py, int contentX, int contentW, int tabScrollPx) {
  const int barX = contentX + 8;
  const int barY = kContentTop + 4;
  const int tabH = kDockChildTabH;
  const int barW = contentW - 16;
  const int tabsW = kHyprChildCount * kTabW + (kHyprChildCount - 1) * kChildTabGap;
  int startX = barX + 4;
  if (tabsW < barW - 8) {
    startX += (barW - 8 - tabsW) / 2;
  } else {
    startX -= tabScrollPx;
  }

  for (int i = 0; i < kHyprChildCount; ++i) {
    const int tx = startX + i * (kTabW + kChildTabGap);
    if (px >= tx && px < tx + kTabW && py >= barY && py < barY + tabH)
      return i;
  }
  return -1;
}

// Paint.
void paint_hyprland_tab(App& app, cairo_t* cr, int contentX, int contentW,
                        double cardX, double cardW, double glassOv,
                        double dockMatA, double paintPointerYOffset, int) {
  (void)cardX; (void)cardW; (void)dockMatA; (void)paintPointerYOffset;

  // Paint child-tab bar
  float a_r = 0.769f, a_g = 0.659f, a_b = 0.941f;
  float t_r = 1.0f, t_g = 1.0f, t_b = 1.0f;
  float s_r = 0.102f, s_g = 0.075f, s_b = 0.188f;
  float o_r = 0.478f, o_g = 0.416f, o_b = 0.588f;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  (void)o_r; (void)o_g; (void)o_b;
  paint_hyprland_child_tab_bar(cr, contentX, contentW, t_r, t_g, t_b, a_r, a_g, a_b, s_r, s_g, s_b, glassOv,
                               app.hyprlandChildTab, app.hyprlandTabScrollPx);

  // Dispatch to active child tab
  const int ct = kHyprlandContentTop;
  switch (app.hyprlandChildTab) {
    case 0: m3::detail::hyprlandGeneralM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 1: m3::detail::hyprlandDecorationM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 2: m3::detail::hyprlandInputM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 3: m3::detail::hyprlandBindsM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 4: m3::detail::hyprlandDwindleM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 5: m3::detail::hyprlandMasterM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 6: m3::detail::hyprlandScrollingM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 7: m3::detail::hyprlandAnimationsM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 8: m3::detail::hyprlandMiscM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 9: m3::detail::hyprlandRenderM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 10: m3::detail::hyprlandCursorM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 11: m3::detail::hyprlandGroupM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 12: m3::detail::hyprlandXwaylandM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 13: m3::detail::hyprlandOpenGLM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
    case 14: m3::detail::hyprlandQuirksM3().paint(app, cr, contentX, contentW, glassOv, ct); break;
  }
}

// M3 pointer dispatch.
bool hyprland_m3_handle_pointer_down(App& app, float px, float py, int contentX, int contentW) {
  // Check child-tab bar hit first
  const int tabHit = hyprland_hit_child_tab(px, py, contentX, contentW, app.hyprlandTabScrollPx);
  if (tabHit >= 0 && tabHit != app.hyprlandChildTab) {
    settings_close_mode_dropdowns(app);
    app.hyprlandChildTab = tabHit;
    hyprland_center_active_tab(app, contentW);
    draw(app);
    return true;
  }

  int subTab = app.hyprlandChildTab;

  // Animations 3-dot popup trigger
  if (subTab == 7) {
    int dotHit = m3::detail::hyprlandAnimationsM3().hitDot(px, py, contentX, contentW, kHyprlandContentTop);
    if (dotHit >= 0) {
      app.hyprlandAnimEditIdx = dotHit;
      app.hyprlandAnimCurveDdOpen = false;
      app.hyprlandAnimStyleDdOpen = false;
      app.hyprlandAnimSpeedEditActive = false;
      draw(app);
      return true;
    }
  }

  switch (subTab) {
    case 0: return m3::detail::hyprlandGeneralM3().handlePointerDown(app, px, py);
    case 1: return m3::detail::hyprlandDecorationM3().handlePointerDown(app, px, py);
    case 2: return m3::detail::hyprlandInputM3().handlePointerDown(app, px, py);
    case 3: return m3::detail::hyprlandBindsM3().handlePointerDown(app, px, py);
    case 4: return m3::detail::hyprlandDwindleM3().handlePointerDown(app, px, py);
    case 5: return m3::detail::hyprlandMasterM3().handlePointerDown(app, px, py);
    case 6: return m3::detail::hyprlandScrollingM3().handlePointerDown(app, px, py);
    case 7: return m3::detail::hyprlandAnimationsM3().handlePointerDown(app, px, py);
    case 8: return m3::detail::hyprlandMiscM3().handlePointerDown(app, px, py);
    case 9: return m3::detail::hyprlandRenderM3().handlePointerDown(app, px, py);
    case 10: return m3::detail::hyprlandCursorM3().handlePointerDown(app, px, py);
    case 11: return m3::detail::hyprlandGroupM3().handlePointerDown(app, px, py);
    case 12: return m3::detail::hyprlandXwaylandM3().handlePointerDown(app, px, py);
    case 13: return m3::detail::hyprlandOpenGLM3().handlePointerDown(app, px, py);
    case 14: return m3::detail::hyprlandQuirksM3().handlePointerDown(app, px, py);
    default: return false;
  }
}

bool hyprland_m3_handle_pointer_up(App& app, float px, float py, int) {
  int subTab = app.hyprlandChildTab;
  switch (subTab) {
    case 0: return m3::detail::hyprlandGeneralM3().handlePointerUp(app, px, py);
    case 1: return m3::detail::hyprlandDecorationM3().handlePointerUp(app, px, py);
    case 2: return m3::detail::hyprlandInputM3().handlePointerUp(app, px, py);
    case 3: return m3::detail::hyprlandBindsM3().handlePointerUp(app, px, py);
    case 4: return m3::detail::hyprlandDwindleM3().handlePointerUp(app, px, py);
    case 5: return m3::detail::hyprlandMasterM3().handlePointerUp(app, px, py);
    case 6: return m3::detail::hyprlandScrollingM3().handlePointerUp(app, px, py);
    case 7: return m3::detail::hyprlandAnimationsM3().handlePointerUp(app, px, py);
    case 8: return m3::detail::hyprlandMiscM3().handlePointerUp(app, px, py);
    case 9: return m3::detail::hyprlandRenderM3().handlePointerUp(app, px, py);
    case 10: return m3::detail::hyprlandCursorM3().handlePointerUp(app, px, py);
    case 11: return m3::detail::hyprlandGroupM3().handlePointerUp(app, px, py);
    case 12: return m3::detail::hyprlandXwaylandM3().handlePointerUp(app, px, py);
    case 13: return m3::detail::hyprlandOpenGLM3().handlePointerUp(app, px, py);
    case 14: return m3::detail::hyprlandQuirksM3().handlePointerUp(app, px, py);
    default: return false;
  }
}

bool hyprland_m3_handle_pointer_move(App& app, float px, float py, int) {
  int subTab = app.hyprlandChildTab;
  bool handled = false;
  switch (subTab) {
    case 0: handled = m3::detail::hyprlandGeneralM3().handlePointerMove(px, py); break;
    case 1: handled = m3::detail::hyprlandDecorationM3().handlePointerMove(px, py); break;
    case 2: handled = m3::detail::hyprlandInputM3().handlePointerMove(px, py); break;
    case 3: handled = m3::detail::hyprlandBindsM3().handlePointerMove(px, py); break;
    case 4: handled = m3::detail::hyprlandDwindleM3().handlePointerMove(px, py); break;
    case 5: handled = m3::detail::hyprlandMasterM3().handlePointerMove(px, py); break;
    case 6: handled = m3::detail::hyprlandScrollingM3().handlePointerMove(px, py); break;
    case 7: handled = m3::detail::hyprlandAnimationsM3().handlePointerMove(px, py); break;
    case 8: handled = m3::detail::hyprlandMiscM3().handlePointerMove(px, py); break;
    case 9: handled = m3::detail::hyprlandRenderM3().handlePointerMove(px, py); break;
    case 10: handled = m3::detail::hyprlandCursorM3().handlePointerMove(px, py); break;
    case 11: handled = m3::detail::hyprlandGroupM3().handlePointerMove(px, py); break;
    case 12: handled = m3::detail::hyprlandXwaylandM3().handlePointerMove(px, py); break;
    case 13: handled = m3::detail::hyprlandOpenGLM3().handlePointerMove(px, py); break;
    case 14: handled = m3::detail::hyprlandQuirksM3().handlePointerMove(px, py); break;
    default: return false;
  }
  if (handled) {
    switch (subTab) {
      case 0: m3::detail::hyprlandGeneralM3().flushSliderValues(app); break;
      case 1: m3::detail::hyprlandDecorationM3().flushSliderValues(app); break;
      case 2: m3::detail::hyprlandInputM3().flushSliderValues(app); break;
      case 3: m3::detail::hyprlandBindsM3().flushSliderValues(app); break;
      case 4: m3::detail::hyprlandDwindleM3().flushSliderValues(app); break;
      case 5: m3::detail::hyprlandMasterM3().flushSliderValues(app); break;
      case 6: m3::detail::hyprlandScrollingM3().flushSliderValues(app); break;
      case 7: m3::detail::hyprlandAnimationsM3().flushSliderValues(app); break;
      case 8: m3::detail::hyprlandMiscM3().flushSliderValues(app); break;
      case 9: m3::detail::hyprlandRenderM3().flushSliderValues(app); break;
      case 10: m3::detail::hyprlandCursorM3().flushSliderValues(app); break;
      case 11: m3::detail::hyprlandGroupM3().flushSliderValues(app); break;
      case 12: m3::detail::hyprlandXwaylandM3().flushSliderValues(app); break;
      case 13: m3::detail::hyprlandOpenGLM3().flushSliderValues(app); break;
      case 14: m3::detail::hyprlandQuirksM3().flushSliderValues(app); break;
    }
  }
  return handled;
}

void hyprland_m3_handle_pointer_leave(int) {
  for (int i = 0; i < kHyprChildCount; ++i) {
    switch (i) {
      case 0: m3::detail::hyprlandGeneralM3().handlePointerLeave(); break;
      case 1: m3::detail::hyprlandDecorationM3().handlePointerLeave(); break;
      case 2: m3::detail::hyprlandInputM3().handlePointerLeave(); break;
      case 3: m3::detail::hyprlandBindsM3().handlePointerLeave(); break;
      case 4: m3::detail::hyprlandDwindleM3().handlePointerLeave(); break;
      case 5: m3::detail::hyprlandMasterM3().handlePointerLeave(); break;
      case 6: m3::detail::hyprlandScrollingM3().handlePointerLeave(); break;
      case 7: m3::detail::hyprlandAnimationsM3().handlePointerLeave(); break;
      case 8: m3::detail::hyprlandMiscM3().handlePointerLeave(); break;
      case 9: m3::detail::hyprlandRenderM3().handlePointerLeave(); break;
      case 10: m3::detail::hyprlandCursorM3().handlePointerLeave(); break;
      case 11: m3::detail::hyprlandGroupM3().handlePointerLeave(); break;
      case 12: m3::detail::hyprlandXwaylandM3().handlePointerLeave(); break;
      case 13: m3::detail::hyprlandOpenGLM3().handlePointerLeave(); break;
      case 14: m3::detail::hyprlandQuirksM3().handlePointerLeave(); break;
    }
  }
}

bool hyprland_m3_has_active_slider(const App& app, int) {
  int subTab = app.hyprlandChildTab;
  switch (subTab) {
    case 0: return m3::detail::hyprlandGeneralM3().activeSlider_ >= 0;
    case 1: return m3::detail::hyprlandDecorationM3().activeSlider_ >= 0;
    case 2: return m3::detail::hyprlandInputM3().activeSlider_ >= 0;
    case 3: return m3::detail::hyprlandBindsM3().activeSlider_ >= 0;
    case 4: return m3::detail::hyprlandDwindleM3().activeSlider_ >= 0;
    case 5: return m3::detail::hyprlandMasterM3().activeSlider_ >= 0;
    case 6: return m3::detail::hyprlandScrollingM3().activeSlider_ >= 0;
    case 7: return m3::detail::hyprlandAnimationsM3().activeSlider_ >= 0;
    case 8: return m3::detail::hyprlandMiscM3().activeSlider_ >= 0;
    case 9: return m3::detail::hyprlandRenderM3().activeSlider_ >= 0;
    case 10: return m3::detail::hyprlandCursorM3().activeSlider_ >= 0;
    case 11: return m3::detail::hyprlandGroupM3().activeSlider_ >= 0;
    case 12: return m3::detail::hyprlandXwaylandM3().activeSlider_ >= 0;
    case 13: return m3::detail::hyprlandOpenGLM3().activeSlider_ >= 0;
    case 14: return m3::detail::hyprlandQuirksM3().activeSlider_ >= 0;
    default: return false;
  }
}

// Scroll clamp.
void settings_clamp_hyprland_scroll_px(App& app) {
  const int mx = std::max(0, app.hyprlandContentBottom - kHyprlandContentTop - std::max(120, app.height - kHyprlandContentTop - kSpacingL));
  app.settingsHyprlandScrollPx = std::clamp(app.settingsHyprlandScrollPx, 0, mx);
}

static int hyprland_tab_bar_overflow_px(int contentW) {
  const int barW = contentW - 16;
  const int tabsW = kHyprChildCount * kTabW + (kHyprChildCount - 1) * kChildTabGap;
  return std::max(0, tabsW - (barW - 8));
}

void hyprland_clamp_tab_scroll(App& app, int contentW) {
  const int overflow = hyprland_tab_bar_overflow_px(contentW);
  app.hyprlandTabScrollPx = std::clamp(app.hyprlandTabScrollPx, 0, overflow);
}

void hyprland_center_active_tab(App& app, int contentW) {
  const int overflow = hyprland_tab_bar_overflow_px(contentW);
  if (overflow <= 0) {
    app.hyprlandTabScrollPx = 0;
    return;
  }
  const int tabCenter = app.hyprlandChildTab * (kTabW + kChildTabGap) + kTabW / 2;
  const int barW = contentW - 16;
  const int visibleCenter = (barW - 8) / 2;
  app.hyprlandTabScrollPx = std::clamp(tabCenter - visibleCenter, 0, overflow);
}

bool hyprland_handle_tab_bar_scroll(App& app, float px, float py, double delta_px,
                                    int contentX, int contentW) {
  constexpr int kBarHitPad = 6;
  const int barX = contentX + 8;
  const int barY = kContentTop - kBarHitPad;
  const int barW = contentW - 16;
  const int barH = kDockChildTabH + 8 + kBarHitPad * 2;
  if (px < barX || px >= barX + barW || py < barY || py >= barY + barH)
    return false;
  const int overflow = hyprland_tab_bar_overflow_px(contentW);
  if (overflow <= 0)
    return false;
  const int step = static_cast<int>(std::lround(delta_px));
  if (step == 0) return false;
  app.hyprlandTabScrollPx += step;
  hyprland_clamp_tab_scroll(app, contentW);
  return true;
}
