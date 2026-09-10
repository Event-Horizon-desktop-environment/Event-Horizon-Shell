#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>

#include <cairo/cairo.h>

#include "m3/controls/containers/button.hpp"
#include "m3/controls/input/toggle.hpp"
#include "m3/controls/input/slider.hpp"
#include "m3/core/label.hpp"
#include "m3/core/primitives/box.hpp"

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_serialize.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"

#include "ux/settings/settings_tab_dock_appearance/settings_tab_dock_appearance.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"

#include "desktop_shell/common/log/debug_log.hpp"

extern void draw(App& app);

// Renderer dropdown (Settings child tab).
// Defined before DockTabM3State because paint() and input handlers use them.
namespace m3::detail {

inline constexpr const char* kRendererLabels[] = {"Vulkan (GPU)", "Cairo (CPU, low memory)"};
inline constexpr int kRendererCount = 2;
inline constexpr const char* kRendererValues[] = {"vulkan", "cairo"};

inline int renderer_ui_index(const App& app) {
  for (int i = 0; i < kRendererCount; ++i)
    if (app.settings.renderer == kRendererValues[i]) return i;
  return 0;
}

inline int renderer_card_top();

// Feed labels/selection/anchor once per frame; called from paint, popup
// painting and hit tests so geometry is never stale.
inline void renderer_dd_sync(App& app, int contentX, int contentW);

} // namespace m3::detail

// Persistent M3 widget state for the dock settings tab.
// One instance lives for the lifetime of the settings process.  The paint
// function syncs geometry + values from App::settings; the input functions
// dispatch pointer events to the correct widget.

namespace m3::detail {

struct DockTabM3State {
  // Card heights (fixed).
  static constexpr int kVisCardH = 52 + kDockVisRowPitch * kDockVisToggleRows + 24;
  static constexpr int kRendererCardH = 52 + kDockVisRowPitch + 24;
  static constexpr int kAppearCardH = kSpacingXL + kSpacingS +
      2 * kSliderRowH + kSpacingM +
      4 * kSliderRowH + kBorderToggleBandH + kSpacingM +
      1 * kSliderRowH + kBorderToggleBandH + kSpacingM +
      2 * kSliderRowH +
      kSpacingXL;
  // Card top positions are computed dynamically in paint() (widgets first).

  // Visibility & behaviour card toggles.
  Toggle showDock;
  Toggle autoHide;
  Toggle embeddedWidgets;
  Toggle groupApps;
  Toggle tooltips;
  Toggle trayPill;
  Toggle trayPillRunning;

  // Appearance card sliders.
  Slider radiusSlider;
  Slider scaleSlider;
  Slider iconSizeSlider;
  Slider iconSpacingSlider;
  Slider bottomGapSlider;
  Slider borderSizeSlider;
  Slider borderHueSlider;
  Slider opacitySlider;
  Slider barHeightSlider;

  // Extra toggles (inside the appearance card).
  Toggle autoBarHeight;
  Toggle dockBorder;

  // Child tabs.
  int activeChildTab_ = 0;    // 0 = Settings, 1 = Widgets, 2 = Appearance

  static constexpr int kChildTabGap = 0;

  enum { kTabSettings = 0, kTabWidgets = 1, kTabAppearance = 2 };

  // State.
  int activeSlider_ = -1;     // -1 = none, 0..8 = slider index

  // Cached colours, refreshed each paint.
  float accentR_ = 0.769f, accentG_ = 0.659f, accentB_ = 0.941f;
  float surfaceR_ = 0.102f, surfaceG_ = 0.075f, surfaceB_ = 0.188f;
  float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
  float outlineR_ = 0.478f, outlineG_ = 0.416f, outlineB_ = 0.588f;

  // Resolve and push colours to all widgets.
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
    auto applyBase = [&](auto& w) {
      w.setAccentColor(accentR_, accentG_, accentB_);
      w.setSurfaceColor(surfaceR_, surfaceG_, surfaceB_);
      w.setTextColor(textR_, textG_, textB_);
    };
    auto applyAll = [&](auto& w) {
      applyBase(w);
      w.setOutlineColor(outlineR_, outlineG_, outlineB_);
    };
    applyAll(showDock); applyAll(autoHide); applyAll(embeddedWidgets);
    applyAll(groupApps); applyAll(tooltips); applyAll(trayPill); applyAll(trayPillRunning);
    applyBase(radiusSlider); applyBase(scaleSlider); applyBase(iconSizeSlider);
    applyBase(iconSpacingSlider); applyBase(bottomGapSlider); applyBase(borderSizeSlider);
    applyBase(borderHueSlider); applyBase(opacitySlider); applyBase(barHeightSlider);
    applyAll(autoBarHeight); applyAll(dockBorder);
  }

  // Refresh hover from the current pointer position (called during paint).
  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(showDock); setH(autoHide); setH(embeddedWidgets);
    setH(groupApps); setH(tooltips); setH(trayPill); setH(trayPillRunning);
    setH(autoBarHeight); setH(dockBorder);

    Slider* sliders[] = {&radiusSlider, &scaleSlider, &iconSizeSlider,
                         &iconSpacingSlider, &bottomGapSlider, &borderSizeSlider,
                         &borderHueSlider, &opacitySlider, &barHeightSlider};
    for (auto* s : sliders) setH(*s);
  }

  // Child tab bar.
  static void paintChildTabBar(cairo_t* cr, int contentX, int contentW,
                                const float textR, const float textG, const float textB,
                                int activeTab) {
    constexpr int kTabW = 130;
    const int barX = contentX + 8;
    const int barY = kContentTop;
    const int barW = contentW - 16;

    // Background strip
    {
      m3::Box bg;
      bg.setColor(textR, textG, textB, 0.04f);
      bg.setRadius(8.0f);
      bg.setGeometry(static_cast<float>(barX), static_cast<float>(barY),
                     static_cast<float>(barW), static_cast<float>(kDockChildTabH + 6));
      bg.setGlassy(true);
      bg.paint(cr);
    }

    const int tabY = barY + 3;
    const int tabH = kDockChildTabH;

    static const char* kChildLabels[] = {"Settings", "Widgets", "Appearance"};
    for (int i = 0; i < 3; ++i) {
      const int tx = barX + 4 + i * (kTabW + kChildTabGap);
      const bool sel = (i == activeTab);

      if (sel) {
        m3::Box selBg;
        selBg.setColor(textR, textG, textB, 0.10f);
        selBg.setRadius(6.0f);
        selBg.setGeometry(static_cast<float>(tx), static_cast<float>(tabY),
                          static_cast<float>(kTabW), static_cast<float>(tabH));
        selBg.setGlassy(true);
        selBg.paint(cr);
      }

      const char* label = kChildLabels[i];
      m3::Label lbl;
      lbl.setText(label);
      lbl.setFontSize(13.0f);
      lbl.setFontWeight(sel ? 600 : 400);
      lbl.setColor(textR, textG, textB, sel ? 0.90f : 0.55f);
      float lw, lh;
      lbl.measureExtents(lw, lh);
      lbl.paintAt(cr, static_cast<float>(tx) + (static_cast<float>(kTabW) - lw) * 0.5f,
                  static_cast<float>(tabY) + (static_cast<float>(tabH) - lh) * 0.5f);
    }
  }

  // Child tab hit test.
  int hitChildTab(float px, float py, int contentX, int) const {
    constexpr int kTabW = 130;
    const int barX = contentX + 8;
    const int barY = kContentTop + 3;
    for (int i = 0; i < 3; ++i) {
      const int tx = barX + 4 + i * (kTabW + kChildTabGap);
      if (px >= tx && px < tx + kTabW && py >= barY && py < barY + kDockChildTabH)
        return i;
    }
    return -1;
  }

  // Y-helpers are defined as lambdas inside paint() using dynamic card positions.

  // Value label helpers.
  static std::string sliderValueLabel(int row, int val, bool follows, int autoBarH) {
    char buf[48];
    if (row == 1)      { std::snprintf(buf, sizeof(buf), "%d %%", val); }
    else if (row == 6) { std::snprintf(buf, sizeof(buf), "%d\xc2\xb0", val); }
    else if (row == 7) { std::snprintf(buf, sizeof(buf), "%d %%", val); }
    else if (row == 8) {
      if (follows) std::snprintf(buf, sizeof(buf), "%d px", autoBarH);
      else         std::snprintf(buf, sizeof(buf), "%d px", val);
    }
    else               { std::snprintf(buf, sizeof(buf), "%d px", val); }
    return buf;
  }

  // Paint the entire dock tab.
  void paint(App& app, cairo_t* cr, int contentX, int contentW,
             double glassOv, int chDock, int autoBarHeightPx) {
    (void)autoBarHeightPx;
    syncColours(app);
    applyColours();

    // Child tab bar (always rendered, before hover/scrolling)
    paintChildTabBar(cr, contentX, contentW, textR_, textG_, textB_, activeChildTab_);

    // Widgets child tab.
    if (activeChildTab_ == kTabWidgets) {
      (void)chDock;
      updateHover(app);

      const float cardX = static_cast<float>(contentX + 8);
      const float cardW = static_cast<float>(contentW - 16);
      const int widgetsTop = kContentTop + kDockChildTabH + 12;

      // Embedded widgets toggle.
      constexpr int kWidgetToggleCardH = kDockWidgetToggleCardH;
      settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(widgetsTop),
                    static_cast<double>(cardW), static_cast<double>(kWidgetToggleCardH), glassOv);
      {
        constexpr float tgH = 26.0f;
        constexpr float tgW = 32.0f;
        const float tgX = cardX + cardW - kCardPad - tgW;
        const float tgY = static_cast<float>(widgetsTop) + (kWidgetToggleCardH - tgH) * 0.5f;
        embeddedWidgets.setSize(Toggle::Size::M);
        embeddedWidgets.setGeometry(tgX, tgY, tgW, tgH);
        embeddedWidgets.setOn(app.settings.dockWidgetsEnabled);
        embeddedWidgets.setEnabled(true);
        settings_show_text(cr, cardX + kCardPad, widgetsTop + 34, "Embedded dock widgets", 15.f, 400, textR_, textG_, textB_, 0.93f);
        settings_show_text(cr, cardX + kCardPad, widgetsTop + 51, "Clock, media, workspaces, and control-center tiles in the dock strip.", 11.f, 400, textR_, textG_, textB_, 0.46f);
        embeddedWidgets.paint(cr, 0);
      }

      // Three-column widget card layout.
      {
        // Resolve colours
        float a_r = accentR_, a_g = accentG_, a_b = accentB_;
        float t_r = textR_, t_g = textG_, t_b = textB_;
        float s_r = surfaceR_, s_g = surfaceG_, s_b = surfaceB_;
        float o_r = outlineR_, o_g = outlineG_, o_b = outlineB_;
        settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

        const double cX = static_cast<double>(cardX);
        const double cW = static_cast<double>(cardW);
        const double colGap = 8.0;
        const double colW = (cW - 2.0 * colGap) / 3.0;
        const double colTop = static_cast<double>(widgetsTop + kWidgetToggleCardH + kCardGap + 12);
        const double scroll = settings_scroll_px(app);
        const double px = app.pointerX;
        const double py = app.pointerY + scroll;
        const auto& ds = app.settings.dockWidgetSlotsDisabled;

        static const char* kColTitles[] = {"LEFT", "CENTER", "RIGHT"};
        const double listY = colTop + 26.0;
        constexpr double kCardH = 54.0;
        constexpr double kCardGapV = 8.0;

        for (int s = 0; s < 3; ++s) {
          const double colX = cX + static_cast<double>(s) * (colW + colGap);
          const auto& widgets = *widgets_for_section_const(app, s);
          // Column title
          if (!widgets.empty() || s == 0) {
            cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 0.55);
            cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 11.0);
            cairo_move_to(cr, colX, colTop);
            cairo_show_text(cr, kColTitles[s]);
          }

          for (size_t i = 0; i < widgets.size(); ++i) {
            const double cy = listY + static_cast<double>(i) * (kCardH + kCardGapV);
            const std::string& wid = widgets[i];
            const bool slotOn = widget_slot_enabled_for_settings(ds, wid);

            // Card background (now with full inner glassy layers, radius-conformant)
            {
              m3::Box bg;
              bg.setColor(s_r, s_g, s_b, 0.90f);
              bg.setRadius(12.0f);
              bg.setGeometry(static_cast<float>(colX), static_cast<float>(cy),
                            static_cast<float>(colW), static_cast<float>(kCardH));
              bg.setGlassy(true);
              bg.paint(cr);
            }

            // Drag grip (three dots)
            settings_draw_drag_handle_row(app, cr, colX + 10.0, cy + kCardH * 0.5, 1.0);

            // Icon box
            const double iconX = colX + 34.0;
            const double iconY = cy + (kCardH - 30.0) * 0.5;
            const double iconS = 30.0;
            {
              m3::Box ibox;
              float ir = a_r * 0.4f + 0.4f;
              float ig = a_g * 0.4f + 0.4f;
              float ib = a_b * 0.4f + 0.4f;
              ibox.setColor(ir, ig, ib, 0.85f);
              ibox.setRadius(8.0f);
              ibox.setGeometry(static_cast<float>(iconX), static_cast<float>(iconY),
                              static_cast<float>(iconS), static_cast<float>(iconS));
              ibox.setGlassy(true);
              ibox.paint(cr);
            }
            material_symbols_draw_glyph(cr, iconX + iconS * 0.5, iconY + iconS * 0.5 + 0.5, 20.0,
                                       dock_widget_material_ligature(wid), t_r, t_g, t_b, 0.95);

            // Widget name
            const double nameX = iconX + iconS + 6.0;
            const double nameW = colX + colW - 8.0 - 84.0 - nameX;
            settings_draw_trimmed_text_line(cr, widget_display_title(wid), nameX, cy + 35.0,
                                           static_cast<size_t>(std::max(0.0, nameW)), 1.0, 13.f, 600);

            // Remove button (rightmost edge)
            const double rmX = colX + colW - 8.0 - 26.0;
            const double rmY = cy + (kCardH - 26.0) * 0.5;
            const bool hovRemove = (px >= rmX && px < rmX + 26.0 && py >= rmY && py < rmY + 26.0);
            settings_draw_row_remove_button(app, cr, rmX, rmY, 26.0, hovRemove, 0.9);

            // Toggle (to the left of remove with a gap)
            const double tgX = rmX - 8.0 - 42.0;
            const double tgY = cy + (kCardH - 24.0) * 0.5;
            const bool hovToggle = (px >= tgX && px < tgX + 42.0 && py >= tgY && py < tgY + 24.0);
            settings_draw_widget_accent_toggle(app, cr, tgX, tgY, hovToggle, 0.8, slotOn);
          }

          // Add widget button
          const double addY = listY + static_cast<double>(widgets.size()) * (kCardH + kCardGapV) + kCardGapV;
          const bool hoverAdd = (px >= colX && px < colX + colW && py >= addY && py < addY + 38.0);
          m3::Button addBtn;
          addBtn.setMinSize(0, 0);
          addBtn.setGlyph("add");
          addBtn.setLabel("Add widget");
          addBtn.setGeometry(static_cast<float>(colX), static_cast<float>(addY),
                            static_cast<float>(colW), 38.0f);
          addBtn.setStyle(m3::Button::Style::Outlined);
          addBtn.setSize(m3::Button::Size::XS);
          addBtn.setAccentColor(a_r, a_g, a_b);
          addBtn.setOutlineColor(o_r, o_g, o_b);
          addBtn.setHovered(hoverAdd);
          addBtn.paint(cr);
        }

        // Drag ghost
        if (app.widgetDragging && app.widgetDragSection >= 0 && app.widgetDragFromIndex >= 0) {
          const double gx = app.pointerX - app.widgetDragGrabDx;
          const double gy = py - app.widgetDragGrabDy;
          cairo_round_rect(cr, gx, gy, colW, kCardH, 12.0);
          cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
          cairo_fill(cr);
          cairo_round_rect(cr, gx, gy, colW, kCardH, 12.0);
          cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
          cairo_set_line_width(cr, 1.0);
          cairo_stroke(cr);
          settings_draw_drag_handle_row(app, cr, gx + 10.0, gy + kCardH * 0.5, 1.0);
          const double gIconX = gx + 34.0;
          const double gIconY = gy + (kCardH - 30.0) * 0.5;
          {
            m3::Box ibox;
            ibox.setColor(1, 1, 1, 0.15f);
            ibox.setRadius(8.0f);
            ibox.setGeometry(static_cast<float>(gIconX), static_cast<float>(gIconY), 30.0f, 30.0f);
            ibox.paint(cr);
          }
          const auto* vg = widgets_for_section_const(app, app.widgetDragSection);
          if (static_cast<size_t>(app.widgetDragFromIndex) < vg->size()) {
            const std::string& gwid = (*vg)[app.widgetDragFromIndex];
            cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
            cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 13.0);
            cairo_move_to(cr, gIconX + 36.0, gy + 35.0);
            cairo_show_text(cr, widget_display_title(gwid).c_str());
          }
        }
        return;
      }
    }

    // Appearance child tab.
    if (activeChildTab_ == kTabAppearance) {
      paint_dock_appearance_tab(app, cr, contentX, contentW, glassOv, settings_scroll_px(app));
      return;
    }

    // Settings child tab.
    updateHover(app);

    const float cardX = static_cast<float>(contentX + 8);
    const float cardW = static_cast<float>(contentW - 16);

    const int kVisCardTop = kContentTop + kDockChildTabH + 12;

    auto visBandTop = [&](int row) { return kVisCardTop + 52 + row * kDockVisRowPitch; };

    // Visibility & behaviour card.
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(kVisCardTop),
                  static_cast<double>(cardW), static_cast<double>(kVisCardH), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(kVisCardTop + 21),
                       "Visibility & behavior");
    settings_show_text(cr, cardX + kCardPad, kVisCardTop + 38,
                       "Control how your dock appears and behaves",
                       11.f, 400, textR_, textG_, textB_, 0.46f);

    struct VisRow {
      Toggle* tg;
      bool* setting;
      const char* title;
      const char* desc;
    };
    VisRow vrows[] = {
      {&showDock, &app.settings.dockShowDock, "Show dock",
       "Render the dock strip; turns off reserve when hidden."},
      {&autoHide, &app.settings.dockAutoHide, "Auto-hide dock",
       "Hide until pointer reaches the trigger zone."},
      {&groupApps, &app.settings.dockGroupApps, "Group running windows",
       "One dock icon merges all windows per application."},
      {&tooltips, &app.settings.dockTooltipsEnabled, "Dock tooltips",
       "Show tooltips when hovering dock icons."},
      {&trayPill, &app.settings.dockPinnedAppsTrayPill, "Pinned apps tray pill",
       "One capsule behind consecutive pinned icons (like the tray)."},
      {&trayPillRunning, &app.settings.dockRunningAppsTrayPill, "Running apps tray pill",
       "One capsule behind consecutive running app icons."},
    };

    for (int i = 0; i < 6; ++i) {
      const int bandTop = visBandTop(i);

      // Divider line between rows
      if (i > 0) {
        cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, static_cast<double>(cardX) + kCardPad + 8.0, bandTop);
        cairo_line_to(cr, static_cast<double>(cardX + cardW) - kCardPad - 8.0, bandTop);
        cairo_stroke(cr);
      }

      const int titleY = bandTop + 36;

      constexpr float tgH = 24.0f;
      constexpr float tgW = 40.0f;
      const float tgX = cardX + cardW - kCardPad - tgW;
      const float tgY = static_cast<float>(bandTop) + (kDockVisRowPitch - tgH) * 0.5f;

      vrows[i].tg->setSize(Toggle::Size::L);
      vrows[i].tg->setGeometry(tgX, tgY, tgW, tgH);
      vrows[i].tg->setOn(*vrows[i].setting);
      vrows[i].tg->setEnabled(true);
      vrows[i].tg->setAccentColor(accentR_, accentG_, accentB_);

      settings_show_text(cr, cardX + kCardPad, titleY, vrows[i].title, 14.f, 500,
                         textR_, textG_, textB_, 0.93f);
      settings_show_text(cr, cardX + kCardPad, titleY + 17, vrows[i].desc, 11.f, 400,
                         textR_, textG_, textB_, 0.46f);

      vrows[i].tg->paint(cr, 0);
    }

    // Renderer card (Vulkan / Cairo picker).
    {
      const int cardTop = renderer_card_top();
      settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cardTop),
                    static_cast<double>(cardW), static_cast<double>(kRendererCardH), glassOv);
      settings_cat_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(cardTop + 21),
                         "Renderer");
      settings_show_text(cr, cardX + kCardPad, cardTop + 38,
                         "Vulkan uses the GPU pipeline; Cairo renders in software with far lower memory.",
                         11.f, 400, textR_, textG_, textB_, 0.46f);
      const int bandTop = cardTop + 52;
      settings_show_text(cr, cardX + kCardPad, bandTop + 36, "Shell renderer", 14.f, 500,
                         textR_, textG_, textB_, 0.93f);
      settings_show_text(cr, cardX + kCardPad, bandTop + 53,
                         "Applies live; switching to Vulkan maps GPU drivers on next frame.",
                         11.f, 400, textR_, textG_, textB_, 0.46f);
      renderer_dd_sync(app, contentX, contentW);
      app.rendererDd.paint_trigger(app, cr, glassOv, settings_scroll_px(app));
    }
  }

  // Input: pointer down.
  bool handlePointerDown(App& app, float px, float py, int contentX, int contentW) {
    // Child tab switch
    const int tabHit = hitChildTab(px, py, contentX, contentW);
    if (tabHit >= 0 && tabHit != activeChildTab_) {
      activeChildTab_ = tabHit;
      activeSlider_ = -1;
      draw(app);
      return true;
    }

    if (activeChildTab_ == kTabAppearance) {
      return false;
    }

    if (activeChildTab_ == kTabWidgets) {
      // Embedded widgets toggle
      if (embeddedWidgets.containsPoint(px, py)) {
        embeddedWidgets.handlePointerDown(px, py);
        return true;
      }

      // Widget card layout geometry (must match paint())
      const double cX = static_cast<double>(contentX + 8);
      const double cW = static_cast<double>(contentW - 16);
      const double colGap = 8.0;
      const double colW = (cW - 2.0 * colGap) / 3.0;
      const int widgetsTop = kContentTop + kDockChildTabH + 12;
      const double colTop = static_cast<double>(widgetsTop + kDockWidgetToggleCardH + kCardGap + 12);
      const double listY = colTop + 26.0;
      constexpr double kCardH = 54.0;
      constexpr double kCardGapV = 8.0;

      for (int s = 0; s < 3; ++s) {
        const double colX = cX + static_cast<double>(s) * (colW + colGap);
        auto& widgets = *widgets_for_section(app, s);

        // Add button hit test
        const double addY = listY + static_cast<double>(widgets.size()) * (kCardH + kCardGapV) + kCardGapV;
        if (px >= colX && px < colX + colW && py >= addY && py < addY + 38.0) {
          debug_log("settings", "dock: opening widget picker");
  app.widgetPickerOpen = true;
          app.widgetPickerForTaskbar = false;
          app.widgetPickerSection = (s == 0) ? "left" : (s == 1) ? "center" : "right";
          app.widgetPickerFilter.clear();
          app.widgetPickerHoverSlot = -1;
          draw(app);
          return true;
        }

        for (size_t i = 0; i < widgets.size(); ++i) {
          const double cy = listY + static_cast<double>(i) * (kCardH + kCardGapV);

          // Remove button hit test (rightmost edge)
          const double rmX = colX + colW - 8.0 - 26.0;
          const double rmY = cy + (kCardH - 26.0) * 0.5;
          if (px >= rmX && px < rmX + 26.0 && py >= rmY && py < rmY + 26.0) {
            const std::string id = widgets[i];
            widgets.erase(widgets.begin() + static_cast<long>(i));
            app.settings.dockWidgetSlotsDisabled.erase(id);
            app.settings.widgetSlotsDisabled.erase(id);
            save_settings(app.settings);
            draw(app);
            return true;
          }

          // Toggle hit test (to the left of remove)
          const double tgX = rmX - 8.0 - 42.0;
          const double tgY = cy + (kCardH - 24.0) * 0.5;
          if (px >= tgX && px < tgX + 42.0 && py >= tgY && py < tgY + 24.0) {
            const std::string& id = widgets[i];
            auto& ds = app.settings.dockWidgetSlotsDisabled;
            const bool wasOff = ds.find(id) != ds.end();
            debug_log("settings", "dock widget toggle id=%s action=%s globalSet=%d taskbarSet=%d",
                      id.c_str(), wasOff ? "ON" : "OFF",
                      app.settings.widgetSlotsDisabled.count(id) ? 1 : 0,
                      app.settings.taskbarWidgetSlotsDisabled.count(id) ? 1 : 0);
            if (wasOff) {
              // Turning ON must clear BOTH sets: load_settings() marks the id
              // in the global widgetSlotsDisabled too, and
              // patch_widget_slot_enabled_into_shell_config unions all sets —
              // a stale global entry would rewrite enabled="false" forever.
              ds.erase(id);
              app.settings.widgetSlotsDisabled.erase(id);
              app.settings.taskbarWidgetSlotsDisabled.erase(id);
            } else {
              ds.insert(id);
            }
            save_settings(app.settings);
            draw(app);
            return true;
          }

          // Drag handle hit test (left side of card)
          const double gripX = colX;
          const double gripW = 34.0;
          if (px >= gripX && px < gripX + gripW && py >= cy && py < cy + kCardH) {
            app.widgetDragArmed = true;
            app.widgetDragging = false;
            app.widgetDragSection = s;
            app.widgetDragTargetSection = s;
            app.widgetDragFromIndex = static_cast<int>(i);
            app.widgetDragPressX = app.pointerX;
            app.widgetDragPressY = app.pointerY;
            const double cardCy = listY + static_cast<double>(i) * (kCardH + kCardGapV);
            const double rowWinTop = cardCy - settings_scroll_px(app);
            app.widgetDragGrabDx = app.pointerX - colX;
            app.widgetDragGrabDy = app.pointerY - rowWinTop;
            draw(app);
            return true;
          }
        }
      }
      return false;
    }

    activeSlider_ = -1;  // reset any stale drag

    // Renderer dropdown: while open, swallow clicks so they don't fall
    // through to toggles underneath; clicking the trigger toggles it.
    if (activeChildTab_ == kTabSettings) {
      renderer_dd_sync(app, contentX, contentW);
      const int scr = settings_scroll_px(app);
      if (app.rendererDd.open()) {
        if (app.rendererDd.hit_trigger(px, py, scr)) {
          app.rendererDd.close();  // click-away on trigger closes
          draw(app);
          return true;
        }
        return true;  // commit happens on pointer-up; consume the down
      }
      if (app.rendererDd.hit_trigger(px, py, scr)) {
        app.rendererDd.open_popup();
        draw(app);
        return true;
      }
    }

    auto tryToggle = [&](Toggle* tg) -> bool {
      if (tg->containsPoint(px, py)) {
        tg->handlePointerDown(px, py);
        return true;
      }
      return false;
    };

    if (tryToggle(&showDock)) return true;
    if (tryToggle(&autoHide)) return true;
    if (tryToggle(&groupApps)) return true;
    if (tryToggle(&tooltips)) return true;
    if (tryToggle(&trayPill)) return true;
    if (tryToggle(&trayPillRunning)) return true;
    if (tryToggle(&autoBarHeight)) return true;
    if (tryToggle(&dockBorder)) return true;

    Slider* sliders[] = {&radiusSlider, &scaleSlider, &iconSizeSlider,
                         &iconSpacingSlider, &bottomGapSlider, &borderSizeSlider,
                         &borderHueSlider, &opacitySlider, &barHeightSlider};
    for (int i = 0; i < 9; ++i) {
      if (sliders[i]->containsPoint(px, py)) {
        sliders[i]->handlePointerDown(px, py);
        activeSlider_ = i;
        return true;
      }
    }

    return false;
  }

  // Input: pointer up.
  bool handlePointerUp(App& app, float px, float py) {
    if (activeChildTab_ == kTabAppearance) {
      activeSlider_ = -1;
      return false;
    }

    bool handled = false;

    auto endToggle = [&](Toggle* tg, bool* setting) {
      if (!tg->pressed()) return;
      tg->handlePointerUp(px, py);
      *setting = !*setting;

      // Special case: auto bar height toggle also updates manual height
      if (tg == &autoBarHeight) {
        if (*setting) {
          app.settings.dockBarFollowsIcons = true;
        } else {
          app.settings.dockBarFollowsIcons = false;
          app.settings.dockManualBarHeightPx =
              settings_dock_preview_auto_bar_px(app.settings);
        }
      }

      handled = true;
    };

    if (activeChildTab_ == kTabWidgets) {
      endToggle(&embeddedWidgets, &app.settings.dockWidgetsEnabled);
      return handled;
    }

    endToggle(&showDock, &app.settings.dockShowDock);
    endToggle(&autoHide, &app.settings.dockAutoHide);
    endToggle(&groupApps, &app.settings.dockGroupApps);
    endToggle(&tooltips, &app.settings.dockTooltipsEnabled);
    endToggle(&trayPill, &app.settings.dockPinnedAppsTrayPill);
    endToggle(&trayPillRunning, &app.settings.dockRunningAppsTrayPill);
    // autoBarHeight handled inside endToggle above
    if (!handled) endToggle(&dockBorder, &app.settings.dockBorderEnabled);

    // Slider drag end
    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&radiusSlider, &scaleSlider, &iconSizeSlider,
                           &iconSpacingSlider, &bottomGapSlider, &borderSizeSlider,
                           &borderHueSlider, &opacitySlider, &barHeightSlider};
      if (activeSlider_ < 9) {
        sliders[activeSlider_]->handlePointerUp(px, py);
      }
      activeSlider_ = -1;
      handled = true;
    }

    return handled;
  }

  // Input: pointer move.
  bool handlePointerMove(float px, float py) {
    if (activeChildTab_ == kTabAppearance || activeChildTab_ == kTabWidgets) {
      return false;
    }

    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&radiusSlider, &scaleSlider, &iconSizeSlider,
                           &iconSpacingSlider, &bottomGapSlider, &borderSizeSlider,
                           &borderHueSlider, &opacitySlider, &barHeightSlider};
      if (activeSlider_ < 9) {
        sliders[activeSlider_]->handlePointerMove(px, py);
      }
      return true;  // consumed — caller should redraw
    }
    return false;    // idle move, no redraw needed (hover handled in paint)
  }

  // Input: pointer leave.
  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(showDock); reset(autoHide); reset(embeddedWidgets);
    reset(groupApps); reset(tooltips); reset(trayPill); reset(trayPillRunning);
    reset(radiusSlider); reset(scaleSlider); reset(iconSizeSlider);
    reset(iconSpacingSlider); reset(bottomGapSlider); reset(borderSizeSlider);
    reset(borderHueSlider); reset(opacitySlider); reset(barHeightSlider);
    reset(autoBarHeight); reset(dockBorder);
    // Appearance tab slider state is reset via app.sliderDrag in the event handler
  }

  // Apply slider-drag values back to App::settings.
  void flushSliderValues(App& app) const {
    if (activeChildTab_ != kTabSettings) return;
    if (activeSlider_ < 0 || activeSlider_ >= 9) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&radiusSlider, &scaleSlider, &iconSizeSlider,
                                 &iconSpacingSlider, &bottomGapSlider, &borderSizeSlider,
                                 &borderHueSlider, &opacitySlider, &barHeightSlider};
      return sliders[activeSlider_]->value();
    }();
    switch (activeSlider_) {
      case 0: app.settings.dockRadius = static_cast<int>(v); break;
      case 1: app.settings.dockScale = static_cast<double>(v) / 100.0; break;
      case 2: app.settings.dockIconSize = static_cast<int>(v); break;
      case 3: app.settings.dockIconSpacing = static_cast<int>(v); break;
      case 4: app.settings.dockBottomGap = static_cast<int>(v); break;
      case 5: app.settings.dockBorderSize = static_cast<int>(v); break;
      case 6: app.settings.dockBorderHue = static_cast<int>(v); break;
      case 7: app.settings.dockOpacity = static_cast<int>(v); break;
      case 8: app.settings.dockManualBarHeightPx = static_cast<int>(v); break;
    }
  }
};

// Singleton — one instance for the lifetime of the settings process.
// C++17 inline magic-static is shared across all TUs that include this header.
inline DockTabM3State& dockM3() {
  static DockTabM3State s;
  return s;
}

inline bool dockM3_is_appearance_child_tab() {
  return dockM3().activeChildTab_ == DockTabM3State::kTabAppearance;
}

inline bool dockM3_is_widgets_child_tab() {
  return dockM3().activeChildTab_ == DockTabM3State::kTabWidgets;
}

inline int renderer_card_top() {
  return kContentTop + kDockChildTabH + 12 + DockTabM3State::kVisCardH + 12;
}

inline void renderer_dd_sync(App& app, int contentX, int contentW) {
  const int cardTop = renderer_card_top();
  const int cx = contentX + 8 + (contentW - 16) - kCardPad - kSettingsComboW;
  const int cy = cardTop + 52 + (kDockVisRowPitch - kSettingsComboH) / 2;
  app.rendererDd.set_labels(kRendererLabels, kRendererCount);
  app.rendererDd.set_selected(renderer_ui_index(app));
  app.rendererDd.set_row_h(kSettingsDdRowH);
  app.rendererDd.set_anchor(cx, cy, kSettingsComboW, kSettingsComboH);
}

} // namespace m3::detail
