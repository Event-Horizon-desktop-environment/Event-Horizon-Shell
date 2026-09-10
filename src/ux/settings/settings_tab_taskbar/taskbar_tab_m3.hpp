#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include <cairo/cairo.h>

#include "m3/controls/containers/button.hpp"
#include "m3/controls/input/toggle.hpp"
#include "m3/controls/input/slider.hpp"
#include "m3/core/label.hpp"
#include "m3/core/primitives/box.hpp"

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/settings_serialize.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"

#include "ux/settings/settings_tab_taskbar/settings_tab_taskbar.hpp"

#include "desktop_shell/common/log/debug_log.hpp"

extern void draw(App& app);
extern const char* const kWidthModeLabels[];

namespace m3::detail {

struct TaskbarTabM3State {
  // Layout constants.
  static constexpr int kTbVisToggleRows = 8;
  static constexpr int kTbVisCardH = 52 + kDockVisRowPitch * kTbVisToggleRows + 24;

  // Settings tab toggles (rows 0,2,3,4,5,6; row 1 = width mode combo).
  Toggle showTaskbar;
  Toggle positionTop;
  Toggle groupApps;
  Toggle autoHide;
  Toggle tooltips;
  Toggle trayPill;
  Toggle trayPillRunning;

  // Widgets tab toggle.
  Toggle embeddedWidgets;

  // Appearance card sliders.
  Slider heightSlider;
  Slider radiusSlider;
  Slider opacitySlider;
  Slider iconSizeSlider;
  Slider iconSpacingSlider;
  Slider floatingGapSlider;
  Slider edgeGapSlider;
  Slider exclusiveZoneSlider;
  Slider scaleSlider;
  Slider borderSizeSlider;

  // Appearance card toggles.
  Toggle taskbarBorder;

  // Child tabs.
  int activeChildTab_ = 0;
  static constexpr int kChildTabGap = 0;
  enum { kTabSettings = 0, kTabWidgets = 1, kTabAppearance = 2 };

  // State.
  int activeSlider_ = -1;

  // Cached colours.
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
    applyAll(showTaskbar); applyAll(positionTop); applyAll(groupApps);
    applyAll(autoHide); applyAll(tooltips); applyAll(trayPill); applyAll(trayPillRunning);
    applyAll(embeddedWidgets);
    applyBase(heightSlider); applyBase(radiusSlider); applyBase(opacitySlider);
    applyBase(iconSizeSlider); applyBase(iconSpacingSlider); applyBase(floatingGapSlider);
    applyBase(edgeGapSlider); applyBase(exclusiveZoneSlider);
    applyBase(scaleSlider); applyBase(borderSizeSlider);
    applyAll(taskbarBorder);
  }

  // Refresh hover from the current pointer position.
  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;

    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(showTaskbar); setH(positionTop); setH(groupApps);
    setH(autoHide); setH(tooltips); setH(trayPill); setH(trayPillRunning);
    setH(embeddedWidgets);
    setH(taskbarBorder);

    Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider,
                         &iconSizeSlider, &iconSpacingSlider, &floatingGapSlider,
                         &edgeGapSlider, &exclusiveZoneSlider,
                         &scaleSlider, &borderSizeSlider};
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

  // Value label helpers.
  static std::string sliderValueLabel(int row, int val) {
    char buf[48];
    if (row == 2)      { std::snprintf(buf, sizeof(buf), "%d %%", val); }
    else if (row == 6) { std::snprintf(buf, sizeof(buf), "%d %%", val); }
    else if (row == 7) { std::snprintf(buf, sizeof(buf), "%d px", val); }
    else               { std::snprintf(buf, sizeof(buf), "%d px", val); }
    return buf;
  }

  // Width-mode combo geometry helpers.
  void widthModeComboGeom(int contentX, int contentW,
                          int& cbx, int& cby, int& cbw, int& cbh) const {
    const int kVisCardTop = kContentTop + kDockChildTabH + 12;
    const int rowY = kVisCardTop + 52 + 1 * kDockVisRowPitch;
    const int cardX = contentX + 8;
    const int cardW = contentW - 16;
    cbx = static_cast<int>(cardX + cardW - kCardPad - static_cast<double>(kSettingsComboW));
    cby = rowY + (kDockVisRowPitch - kSettingsComboH) / 2;
    cbw = kSettingsComboW;
    cbh = kSettingsComboH;
  }

  int widthModeDropdownY(int contentX, int contentW) const {
    int cbx, cby, cbw, cbh;
    widthModeComboGeom(contentX, contentW, cbx, cby, cbw, cbh);
    return cby + cbh + 2;
  }

  // Paint.
  void paint(App& app, cairo_t* cr, int contentX, int contentW,
             double glassOv, int) {
    syncColours(app);
    applyColours();

    // Child tab bar (always rendered)
    paintChildTabBar(cr, contentX, contentW, textR_, textG_, textB_, activeChildTab_);

    // Widgets child tab.
    if (activeChildTab_ == kTabWidgets) {
      updateHover(app);

      const float cardX = static_cast<float>(contentX + 8);
      const float cardW = static_cast<float>(contentW - 16);
      const int widgetsTop = kContentTop + kDockChildTabH + 12;

      // Embedded widgets toggle card.
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
        embeddedWidgets.setOn(app.settings.taskbarWidgetsEnabled);
        embeddedWidgets.setEnabled(true);
        settings_show_text(cr, cardX + kCardPad, widgetsTop + 34, "Embedded taskbar widgets", 15.f, 400,
                           textR_, textG_, textB_, 0.93f);
        settings_show_text(cr, cardX + kCardPad, widgetsTop + 51, "Show widgets inside the taskbar strip.", 11.f, 400,
                           textR_, textG_, textB_, 0.46f);
        embeddedWidgets.paint(cr, 0);
      }

      // Three-column widget card layout.
      {
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
        const auto& ds = app.settings.taskbarWidgetSlotsDisabled;

        static const char* kColTitles[] = {"LEFT", "CENTER", "RIGHT"};
        const double listY = colTop + 26.0;
        constexpr double kCardH = 54.0;
        constexpr double kCardGapV = 8.0;

        for (int s = 0; s < 3; ++s) {
          const double colX = cX + static_cast<double>(s) * (colW + colGap);
          const auto& widgets = *widgets_for_section_const(app, s);
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

            // Drag grip
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

            // Toggle (to the left of remove)
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

    // Settings child tab.
    if (activeChildTab_ == kTabSettings) {
      updateHover(app);

      const float cardX = static_cast<float>(contentX + 8);
      const float cardW = static_cast<float>(contentW - 16);
      const int kVisCardTop = kContentTop + kDockChildTabH + 12;

      auto visBandTop = [&](int row) { return kVisCardTop + 52 + row * kDockVisRowPitch; };

      // Visibility & behaviour card.
      settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(kVisCardTop),
                    static_cast<double>(cardW), static_cast<double>(kTbVisCardH), glassOv);
      settings_cat_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(kVisCardTop + 21),
                         "Taskbar");
      settings_show_text(cr, cardX + kCardPad, kVisCardTop + 38,
                         "Control how your taskbar appears and behaves",
                         11.f, 400, textR_, textG_, textB_, 0.46f);

      // Settings toggles + width mode combo row
      // Row 0: Show taskbar
      {
        const int bandTop = visBandTop(0);
        const int titleY = bandTop + 36;

        cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, static_cast<double>(cardX + kCardPad + 8), bandTop);
        cairo_line_to(cr, static_cast<double>(cardX + cardW - kCardPad - 8), bandTop);
        cairo_stroke(cr);

        constexpr float tgH = 24.0f;
        constexpr float tgW = 40.0f;
        const float tgX = cardX + cardW - kCardPad - tgW;
        const float tgY = static_cast<float>(bandTop) + (kDockVisRowPitch - tgH) * 0.5f;
        showTaskbar.setSize(Toggle::Size::L);
        showTaskbar.setGeometry(tgX, tgY, tgW, tgH);
        showTaskbar.setOn(app.settings.taskbarEnabled);
        showTaskbar.setEnabled(true);
        showTaskbar.setAccentColor(accentR_, accentG_, accentB_);

        settings_show_text(cr, cardX + kCardPad, titleY, "Show taskbar", 14.f, 500,
                           textR_, textG_, textB_, 0.93f);
        settings_show_text(cr, cardX + kCardPad, titleY + 17, "Render the taskbar at the bottom or top.", 11.f, 400,
                           textR_, textG_, textB_, 0.46f);
        showTaskbar.paint(cr, 0);
      }

      // Row 1: Width mode (combo - not a toggle)
      {
        const int bandTop = visBandTop(1);

        cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, static_cast<double>(cardX + kCardPad + 8), bandTop);
        cairo_line_to(cr, static_cast<double>(cardX + cardW - kCardPad - 8), bandTop);
        cairo_stroke(cr);

        int cbx, cby, cbw, cbh;
        widthModeComboGeom(contentX, contentW, cbx, cby, cbw, cbh);
        settings_show_text(cr, cardX + kCardPad, bandTop + 36, "Width mode", 14.f, 500,
                           textR_, textG_, textB_, 0.93f);
        settings_show_text(cr, cardX + kCardPad, bandTop + 53, "Floating, edge-to-edge, or full fill.", 11.f, 400,
                           textR_, textG_, textB_, 0.46f);
        settings_paint_combo_closed(app, cr, cbx, cby, cbw, cbh, glassOv,
                                    kWidthModeLabels[std::clamp(app.settings.taskbarWidthMode, 0, 2)],
                                    app.taskbarWidthModeDropdownOpen);
      }

      // Row 2: Position at top
      {
        const int bandTop = visBandTop(2);
        const int titleY = bandTop + 36;

        cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, static_cast<double>(cardX + kCardPad + 8), bandTop);
        cairo_line_to(cr, static_cast<double>(cardX + cardW - kCardPad - 8), bandTop);
        cairo_stroke(cr);

        constexpr float tgH = 24.0f;
        constexpr float tgW = 40.0f;
        const float tgX = cardX + cardW - kCardPad - tgW;
        const float tgY = static_cast<float>(bandTop) + (kDockVisRowPitch - tgH) * 0.5f;
        positionTop.setSize(Toggle::Size::L);
        positionTop.setGeometry(tgX, tgY, tgW, tgH);
        positionTop.setOn(app.settings.taskbarPositionTop);
        positionTop.setEnabled(true);
        positionTop.setAccentColor(accentR_, accentG_, accentB_);

        settings_show_text(cr, cardX + kCardPad, titleY, "Position at top", 14.f, 500,
                           textR_, textG_, textB_, 0.93f);
        settings_show_text(cr, cardX + kCardPad, titleY + 17, "Place the taskbar at the top edge of the screen.", 11.f, 400,
                           textR_, textG_, textB_, 0.46f);
        positionTop.paint(cr, 0);
      }

      // Row 3: Group running windows
      {
        const int bandTop = visBandTop(3);
        const int titleY = bandTop + 36;

        cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, static_cast<double>(cardX + kCardPad + 8), bandTop);
        cairo_line_to(cr, static_cast<double>(cardX + cardW - kCardPad - 8), bandTop);
        cairo_stroke(cr);

        constexpr float tgH = 24.0f;
        constexpr float tgW = 40.0f;
        const float tgX = cardX + cardW - kCardPad - tgW;
        const float tgY = static_cast<float>(bandTop) + (kDockVisRowPitch - tgH) * 0.5f;
        groupApps.setSize(Toggle::Size::L);
        groupApps.setGeometry(tgX, tgY, tgW, tgH);
        groupApps.setOn(app.settings.taskbarGroupApps);
        groupApps.setEnabled(true);
        groupApps.setAccentColor(accentR_, accentG_, accentB_);

        settings_show_text(cr, cardX + kCardPad, titleY, "Group running windows", 14.f, 500,
                           textR_, textG_, textB_, 0.93f);
        settings_show_text(cr, cardX + kCardPad, titleY + 17, "Group windows from the same app together.", 11.f, 400,
                           textR_, textG_, textB_, 0.46f);
        groupApps.paint(cr, 0);
      }

      // Row 4: Auto-hide
      {
        const int bandTop = visBandTop(4);
        const int titleY = bandTop + 36;

        cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, static_cast<double>(cardX + kCardPad + 8), bandTop);
        cairo_line_to(cr, static_cast<double>(cardX + cardW - kCardPad - 8), bandTop);
        cairo_stroke(cr);

        constexpr float tgH = 24.0f;
        constexpr float tgW = 40.0f;
        const float tgX = cardX + cardW - kCardPad - tgW;
        const float tgY = static_cast<float>(bandTop) + (kDockVisRowPitch - tgH) * 0.5f;
        autoHide.setSize(Toggle::Size::L);
        autoHide.setGeometry(tgX, tgY, tgW, tgH);
        autoHide.setOn(app.settings.taskbarAutoHide);
        autoHide.setEnabled(true);
        autoHide.setAccentColor(accentR_, accentG_, accentB_);

        settings_show_text(cr, cardX + kCardPad, titleY, "Auto-hide", 14.f, 500,
                           textR_, textG_, textB_, 0.93f);
        settings_show_text(cr, cardX + kCardPad, titleY + 17, "Automatically hide the taskbar when not in use.", 11.f, 400,
                           textR_, textG_, textB_, 0.46f);
        autoHide.paint(cr, 0);
      }

      // Row 5: Tooltips
      {
        const int bandTop = visBandTop(5);
        const int titleY = bandTop + 36;

        cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, static_cast<double>(cardX + kCardPad + 8), bandTop);
        cairo_line_to(cr, static_cast<double>(cardX + cardW - kCardPad - 8), bandTop);
        cairo_stroke(cr);

        constexpr float tgH = 24.0f;
        constexpr float tgW = 40.0f;
        const float tgX = cardX + cardW - kCardPad - tgW;
        const float tgY = static_cast<float>(bandTop) + (kDockVisRowPitch - tgH) * 0.5f;
        tooltips.setSize(Toggle::Size::L);
        tooltips.setGeometry(tgX, tgY, tgW, tgH);
        tooltips.setOn(app.settings.taskbarTooltipsEnabled);
        tooltips.setEnabled(true);
        tooltips.setAccentColor(accentR_, accentG_, accentB_);

        settings_show_text(cr, cardX + kCardPad, titleY, "Tooltips", 14.f, 500,
                           textR_, textG_, textB_, 0.93f);
        settings_show_text(cr, cardX + kCardPad, titleY + 17, "Show tooltips when hovering over app icons.", 11.f, 400,
                           textR_, textG_, textB_, 0.46f);
        tooltips.paint(cr, 0);
      }

      // Row 6: Pinned apps tray pill
      {
        const int bandTop = visBandTop(6);
        const int titleY = bandTop + 36;

        cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, static_cast<double>(cardX + kCardPad + 8), bandTop);
        cairo_line_to(cr, static_cast<double>(cardX + cardW - kCardPad - 8), bandTop);
        cairo_stroke(cr);

        constexpr float tgH = 24.0f;
        constexpr float tgW = 40.0f;
        const float tgX = cardX + cardW - kCardPad - tgW;
        const float tgY = static_cast<float>(bandTop) + (kDockVisRowPitch - tgH) * 0.5f;
        trayPill.setSize(Toggle::Size::L);
        trayPill.setGeometry(tgX, tgY, tgW, tgH);
        trayPill.setOn(app.settings.taskbarPinnedAppsTrayPill);
        trayPill.setEnabled(true);
        trayPill.setAccentColor(accentR_, accentG_, accentB_);

        settings_show_text(cr, cardX + kCardPad, titleY, "Pinned apps tray pill", 14.f, 500,
                           textR_, textG_, textB_, 0.93f);
        settings_show_text(cr, cardX + kCardPad, titleY + 17, "Draw a pill-shaped background behind pinned apps.", 11.f, 400,
                           textR_, textG_, textB_, 0.46f);
        trayPill.paint(cr, 0);
      }

      // Row 7: Running apps tray pill
      {
        const int bandTop = visBandTop(7);
        const int titleY = bandTop + 36;

        cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, static_cast<double>(cardX + kCardPad + 8), bandTop);
        cairo_line_to(cr, static_cast<double>(cardX + cardW - kCardPad - 8), bandTop);
        cairo_stroke(cr);

        constexpr float tgH = 24.0f;
        constexpr float tgW = 40.0f;
        const float tgX = cardX + cardW - kCardPad - tgW;
        const float tgY = static_cast<float>(bandTop) + (kDockVisRowPitch - tgH) * 0.5f;
        trayPillRunning.setSize(Toggle::Size::L);
        trayPillRunning.setGeometry(tgX, tgY, tgW, tgH);
        trayPillRunning.setOn(app.settings.taskbarRunningAppsTrayPill);
        trayPillRunning.setEnabled(true);
        trayPillRunning.setAccentColor(accentR_, accentG_, accentB_);

        settings_show_text(cr, cardX + kCardPad, titleY, "Running apps tray pill", 14.f, 500,
                           textR_, textG_, textB_, 0.93f);
        settings_show_text(cr, cardX + kCardPad, titleY + 17, "Draw a pill-shaped background behind running apps.", 11.f, 400,
                           textR_, textG_, textB_, 0.46f);
        trayPillRunning.paint(cr, 0);
      }
      return;
    }

    // Appearance child tab.
    if (activeChildTab_ == kTabAppearance) {
      paintAppearance(app, cr, contentX, contentW, glassOv);
      return;
    }
  }

  // Appearance tab helpers.
  static void appearSliderGeom(int contentX, int contentW, int idx,
                               int& trX, int& trY, int& trW,
                               int& cardX, int& cardY, int& cardW) {
    constexpr int kTopMargin = 72;
    constexpr int kCardH = 92;
    constexpr int kGap = 12;
    constexpr int kPad = 20;
    constexpr int kSliderY = 50;

    const int col = idx % 2;
    const int row = idx / 2;
    const int colW = (contentW * 68) / 100;
    const int colX = contentX + (contentW - colW) / 2;
    cardW = (colW - kGap) / 2;
    if (cardW < 160) cardW = 160;
    cardX = colX + col * (cardW + kGap);
    cardY = kContentTop + kTopMargin + row * (kCardH + kGap);
    trX = cardX + kPad;
    trW = cardW - kPad - kPad;
    if (trW < 40) trW = 40;
    trY = cardY + kSliderY;
  }

  static void drawValuePill(cairo_t* cr, int cx, int cy, int cw, const char* text,
                            float textR, float textG, float textB) {
    if (!text || !text[0]) return;
    cairo_save(cr);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11.0);
    cairo_text_extents_t te;
    cairo_text_extents(cr, text, &te);
    const int padX = 8, padY = 3;
    const int pillW = static_cast<int>(te.width) + padX * 2;
    const int pillH = static_cast<int>(te.height) + padY * 2;
    const int pillX = cx + cw - 20 - pillW;
    const int pillY = cy + 28 - te.height / 2 - padY;

    {
      m3::Box bg;
      bg.setColor(textR, textG, textB, 0.08f);
      bg.setRadius(static_cast<float>(pillH / 2));
      bg.setGeometry(static_cast<float>(pillX), static_cast<float>(pillY),
                     static_cast<float>(pillW), static_cast<float>(pillH));
      bg.setGlassy(true);
      bg.paint(cr);
    }
    cairo_set_source_rgba(cr, textR, textG, textB, 0.60f);
    cairo_move_to(cr, pillX + padX, pillY + te.height + padY - 2.0);
    cairo_show_text(cr, text);
    cairo_restore(cr);
  }

  void paintAppearance(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
    updateHover(app);

    static const char* titles[] = {
      "Height", "Corner Radius", "Opacity", "Icon Size",
      "Icon Spacing", "Taskbar Border", "Scale", "Border Size",
      "Taskbar Margin", "Exclusive Zone"
    };
    const int nItems = 10;
    int sliderVals[10];
    sliderVals[0] = app.settings.taskbarHeight;
    sliderVals[1] = app.settings.taskbarRadius;
    sliderVals[2] = app.settings.taskbarOpacity;
    sliderVals[3] = app.settings.taskbarIconSize;
    sliderVals[4] = app.settings.taskbarIconSpacing;
    sliderVals[5] = app.settings.taskbarFloatingAmount;
    sliderVals[6] = static_cast<int>(std::lround(std::clamp(app.settings.taskbarScale, 0.5, 2.0) * 100.0));
    sliderVals[7] = app.settings.taskbarBorderSize;
    sliderVals[8] = app.settings.taskbarEdgeGap;
    sliderVals[9] = app.settings.taskbarExclusiveZoneGap;
    float sliderMin[10] = {24, 0, 0, 0, 0, 0, 50, 1, 0, 0};
    float sliderMax[10] = {120, 50, 100, 96, 50, 50, 150, 12, 25, 100};

    Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider,
                         &iconSizeSlider, &iconSpacingSlider, &floatingGapSlider,
                         &scaleSlider, &borderSizeSlider,
                         &edgeGapSlider, &exclusiveZoneSlider};

    for (int i = 0; i < nItems; ++i) {
      int trX, trY, trW, cardX, cardY, cardW;
      appearSliderGeom(contentX, contentW, i, trX, trY, trW, cardX, cardY, cardW);

      settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cardY),
                    static_cast<double>(cardW), 92.0, glassOv);

      settings_show_text(cr, static_cast<double>(cardX + 20), static_cast<double>(cardY + 28),
                         titles[i], 13.f, 500,
                         textR_, textG_, textB_, 0.90f);

      if (i == 5) {
        // Border toggle card.
        constexpr float tgH = 24.0f;
        constexpr float tgW = 40.0f;
        const float tgX = static_cast<float>(cardX + cardW - 20 - tgW);
        const float tgY = static_cast<float>(cardY) + (92.0f - tgH) * 0.5f;
        taskbarBorder.setSize(Toggle::Size::L);
        taskbarBorder.setGeometry(tgX, tgY, tgW, tgH);
        taskbarBorder.setOn(app.settings.taskbarBorder);
        taskbarBorder.setEnabled(true);
        {
          taskbarBorder.setAccentColor(accentR_, accentG_, accentB_);
        }
        taskbarBorder.paint(cr, 0);
      } else {
        // Slider card.
        char valStr[64];
        if (i == 2 || i == 6)
          std::snprintf(valStr, sizeof(valStr), "%d %%", sliderVals[i]);
        else
          std::snprintf(valStr, sizeof(valStr), "%d px", sliderVals[i]);

        drawValuePill(cr, cardX, cardY, cardW, valStr, textR_, textG_, textB_);

        sliders[i]->setRange(sliderMin[i], sliderMax[i]);
        sliders[i]->setStep(1.0f);
        sliders[i]->setValue(static_cast<float>(sliderVals[i]));
        sliders[i]->setSize(m3::Slider::Size::M);
        sliders[i]->setGeometry(static_cast<float>(trX), static_cast<float>(trY),
                                static_cast<float>(trW), 28.0f);
        sliders[i]->setShowValueLabel(true);
        sliders[i]->setValueLabel(valStr);
        sliders[i]->paint(cr, 0);
      }
    }
  }

  static int appearanceTabScrollMaxPx() {
    constexpr int kTopMargin = 72;
    constexpr int kCardH = 92;
    constexpr int kGap = 12;
    const int rows = 5;
    return kTopMargin + rows * (kCardH + kGap) + 20;
  }

  // Input: pointer down.
  bool handlePointerDown(App& app, float px, float py, int contentX, int contentW) {
    // Child tab switch
    const int tabHit = hitChildTab(px, py, contentX, contentW);
    if (tabHit >= 0 && tabHit != activeChildTab_) {
      activeChildTab_ = tabHit;
      activeSlider_ = -1;
      app.taskbarWidthModeDropdownOpen = false;
      draw(app);
      return true;
    }

    // Settings tab.
    if (activeChildTab_ == kTabSettings) {
      return handleSettingsPointerDown(app, px, py, contentX, contentW);
    }

    // Widgets tab.
    if (activeChildTab_ == kTabWidgets) {
      return handleWidgetsPointerDown(app, px, py, contentX, contentW);
    }

    // Appearance tab.
    if (activeChildTab_ == kTabAppearance) {
      return handleAppearancePointerDown(px, py, contentX, contentW);
    }

    return false;
  }

  bool handleSettingsPointerDown(App& app, float px, float py, int contentX, int contentW) {
    activeSlider_ = -1;

    // Check width mode combo first (dropdown open)
    if (app.taskbarWidthModeDropdownOpen) {
      int cbx, cby, cbw, cbh;
      widthModeComboGeom(contentX, contentW, cbx, cby, cbw, cbh);
      const int ly = cby + cbh + 2;
      const int lh = 3 * kSettingsDdRowH;
      // Dropdown item hit test
      if (px >= cbx && px < cbx + cbw && py >= ly && py < ly + lh) {
        const int rr = static_cast<int>((py - ly) / kSettingsDdRowH);
        if (rr >= 0 && rr <= 2) {
          app.settings.taskbarWidthMode = rr;
          save_settings(app.settings);
        }
        app.taskbarWidthModeDropdownOpen = false;
        draw(app);
        return true;
      }
      // Combo box toggle (close dropdown)
      if (px >= cbx && px < cbx + cbw && py >= cby && py < cby + cbh) {
        app.taskbarWidthModeDropdownOpen = false;
        draw(app);
        return true;
      }
      // Clicked outside dropdown — close it
      app.taskbarWidthModeDropdownOpen = false;
      draw(app);
      return false; // let old handler fall through for non-combo clicks
    }

    // Check width mode combo click (open dropdown)
    {
      int cbx, cby, cbw, cbh;
      widthModeComboGeom(contentX, contentW, cbx, cby, cbw, cbh);
      if (px >= cbx && px < cbx + cbw && py >= cby && py < cby + cbh) {
        app.taskbarWidthModeDropdownOpen = true;
        draw(app);
        return true;
      }
    }

    // Toggle hit tests
    auto tryToggle = [&](Toggle* tg) -> bool {
      if (tg->containsPoint(px, py)) {
        tg->handlePointerDown(px, py);
        return true;
      }
      return false;
    };

    if (tryToggle(&showTaskbar)) return true;
    if (tryToggle(&positionTop)) return true;
    if (tryToggle(&groupApps)) return true;
    if (tryToggle(&autoHide)) return true;
    if (tryToggle(&tooltips)) return true;
    if (tryToggle(&trayPill)) return true;
    if (tryToggle(&trayPillRunning)) return true;

    return false;
  }

  bool handleWidgetsPointerDown(App& app, float px, float py, int contentX, int contentW) {
    // Embedded widgets toggle
    if (embeddedWidgets.containsPoint(px, py)) {
      embeddedWidgets.handlePointerDown(px, py);
      return true;
    }

    // Three-column layout (must match paint())
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
        debug_log("settings", "taskbar: opening widget picker");
  app.widgetPickerOpen = true;
        app.widgetPickerForTaskbar = true;
        app.widgetPickerSection = (s == 0) ? "left" : (s == 1) ? "center" : "right";
        app.widgetPickerFilter.clear();
        app.widgetPickerHoverSlot = -1;
        draw(app);
        return true;
      }

      for (size_t i = 0; i < widgets.size(); ++i) {
        const double cy = listY + static_cast<double>(i) * (kCardH + kCardGapV);

        // Remove button hit test
        const double rmX = colX + colW - 8.0 - 26.0;
        const double rmY = cy + (kCardH - 26.0) * 0.5;
        if (px >= rmX && px < rmX + 26.0 && py >= rmY && py < rmY + 26.0) {
          const std::string id = widgets[i];
          widgets.erase(widgets.begin() + static_cast<long>(i));
          app.settings.taskbarWidgetSlotsDisabled.erase(id);
          app.settings.widgetSlotsDisabled.erase(id);
          save_settings(app.settings);
          draw(app);
          return true;
        }

        // Toggle hit test
        const double tgX = rmX - 8.0 - 42.0;
        const double tgY = cy + (kCardH - 24.0) * 0.5;
        if (px >= tgX && px < tgX + 42.0 && py >= tgY && py < tgY + 24.0) {
          const std::string& id = widgets[i];
          auto& ds = app.settings.taskbarWidgetSlotsDisabled;
          const bool wasOff = ds.find(id) != ds.end();
          debug_log("settings", "taskbar widget toggle id=%s action=%s globalSet=%d dockSet=%d",
                    id.c_str(), wasOff ? "ON" : "OFF",
                    app.settings.widgetSlotsDisabled.count(id) ? 1 : 0,
                    app.settings.dockWidgetSlotsDisabled.count(id) ? 1 : 0);
          if (wasOff) {
            // Mirror the dock-tab toggle: clear the global set as well or
            // patch_widget_slot_enabled_into_shell_config keeps the widget off.
            ds.erase(id);
            app.settings.widgetSlotsDisabled.erase(id);
            app.settings.dockWidgetSlotsDisabled.erase(id);
          } else {
            ds.insert(id);
          }
          save_settings(app.settings);
          draw(app);
          return true;
        }

        // Drag handle hit test
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

  bool handleAppearancePointerDown(float px, float py, int contentX, int contentW) {
    activeSlider_ = -1;

    // Border toggle (index 5)
    {
      int trX, trY, trW, cardX, cardY, cardW;
      appearSliderGeom(contentX, contentW, 5, trX, trY, trW, cardX, cardY, cardW);
      constexpr float tgH = 24.0f;
      constexpr float tgW = 40.0f;
      const float tgX = static_cast<float>(cardX + cardW - 20 - tgW);
      const float tgY = static_cast<float>(cardY) + (92.0f - tgH) * 0.5f;
      taskbarBorder.setSize(Toggle::Size::L);
      taskbarBorder.setGeometry(tgX, tgY, tgW, tgH);
      if (taskbarBorder.containsPoint(px, py)) {
        taskbarBorder.handlePointerDown(px, py);
        return true;
      }
    }

    Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider,
                         &iconSizeSlider, &iconSpacingSlider, &floatingGapSlider,
                         &scaleSlider, &borderSizeSlider,
                         &edgeGapSlider, &exclusiveZoneSlider};
    float sliderMin[10] = {24, 0, 0, 0, 0, 0, 50, 1, 0, 0};
    float sliderMax[10] = {120, 50, 100, 96, 50, 50, 150, 12, 25, 100};

    for (int i = 0; i < 10; ++i) {
      int trX, trY, trW, cardX, cardY, cardW;
      appearSliderGeom(contentX, contentW, i, trX, trY, trW, cardX, cardY, cardW);

      sliders[i]->setRange(sliderMin[i], sliderMax[i]);
      sliders[i]->setGeometry(static_cast<float>(trX), static_cast<float>(trY),
                              static_cast<float>(trW), 28.0f);

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
    bool handled = false;

    if (activeChildTab_ == kTabAppearance) {
      // Border toggle
      if (taskbarBorder.pressed()) {
        taskbarBorder.handlePointerUp(px, py);
        app.settings.taskbarBorder = !app.settings.taskbarBorder;
        handled = true;
      }
      // Slider drag end
      if (activeSlider_ >= 0) {
        Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider,
                             &iconSizeSlider, &iconSpacingSlider, &floatingGapSlider,
                             &scaleSlider, &borderSizeSlider,
                             &edgeGapSlider, &exclusiveZoneSlider};
        if (activeSlider_ < 10) {
          sliders[activeSlider_]->handlePointerUp(px, py);
        }
        activeSlider_ = -1;
        handled = true;
      }
      activeSlider_ = -1;
      return handled;
    }

    auto endToggle = [&](Toggle* tg, bool* setting) {
      if (!tg->pressed()) return;
      tg->handlePointerUp(px, py);
      *setting = !*setting;
      handled = true;
    };

    if (activeChildTab_ == kTabWidgets) {
      endToggle(&embeddedWidgets, &app.settings.taskbarWidgetsEnabled);
      return handled;
    }

    if (activeChildTab_ == kTabSettings) {
      endToggle(&showTaskbar, &app.settings.taskbarEnabled);
      endToggle(&positionTop, &app.settings.taskbarPositionTop);
      endToggle(&groupApps, &app.settings.taskbarGroupApps);
      endToggle(&autoHide, &app.settings.taskbarAutoHide);
      endToggle(&tooltips, &app.settings.taskbarTooltipsEnabled);
      if (!handled) endToggle(&trayPill, &app.settings.taskbarPinnedAppsTrayPill);
      if (!handled) endToggle(&trayPillRunning, &app.settings.taskbarRunningAppsTrayPill);
    }

    // Slider drag end (for settings tab if any)
    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider,
                           &iconSizeSlider, &iconSpacingSlider, &floatingGapSlider,
                           &scaleSlider, &borderSizeSlider,
                           &edgeGapSlider, &exclusiveZoneSlider};
      if (activeSlider_ < 10) {
        sliders[activeSlider_]->handlePointerUp(px, py);
      }
      activeSlider_ = -1;
      handled = true;
    }

    return handled;
  }

  // Input: pointer move.
  bool handlePointerMove(float px, float py) {
    if (activeChildTab_ == kTabSettings || activeChildTab_ == kTabWidgets) {
      return false;
    }

    if (activeSlider_ >= 0) {
      Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider,
                           &iconSizeSlider, &iconSpacingSlider, &floatingGapSlider,
                           &scaleSlider, &borderSizeSlider,
                           &edgeGapSlider, &exclusiveZoneSlider};
      if (activeSlider_ < 10) {
        sliders[activeSlider_]->handlePointerMove(px, py);
      }
      return true;
    }
    return false;
  }

  // Input: pointer leave.
  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(showTaskbar); reset(positionTop); reset(groupApps);
    reset(autoHide); reset(tooltips); reset(trayPill); reset(trayPillRunning);
    reset(embeddedWidgets);
    reset(heightSlider); reset(radiusSlider); reset(opacitySlider);
    reset(iconSizeSlider); reset(iconSpacingSlider); reset(floatingGapSlider);
    reset(edgeGapSlider); reset(exclusiveZoneSlider);
    reset(scaleSlider); reset(borderSizeSlider);
    reset(taskbarBorder);
  }

  // Apply slider-drag values back to App::settings.
  void flushSliderValues(App& app) const {
    if (activeChildTab_ != kTabAppearance) return;
    if (activeSlider_ < 0 || activeSlider_ >= 10) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider,
                                 &iconSizeSlider, &iconSpacingSlider, &floatingGapSlider,
                                 &scaleSlider, &borderSizeSlider,
                                 &edgeGapSlider, &exclusiveZoneSlider};
      return sliders[activeSlider_]->value();
    }();
    switch (activeSlider_) {
      case 0: app.settings.taskbarHeight = static_cast<int>(v); break;
      case 1: app.settings.taskbarRadius = static_cast<int>(v); break;
      case 2: app.settings.taskbarOpacity = static_cast<int>(v); break;
      case 3: app.settings.taskbarIconSize = static_cast<int>(v); break;
      case 4: app.settings.taskbarIconSpacing = static_cast<int>(v); break;
      case 5: app.settings.taskbarFloatingAmount = static_cast<int>(v); break;
      case 6: app.settings.taskbarScale = static_cast<double>(v) / 100.0; break;
      case 7: app.settings.taskbarBorderSize = static_cast<int>(v); break;
      case 8: app.settings.taskbarEdgeGap = static_cast<int>(v); break;
      case 9: app.settings.taskbarExclusiveZoneGap = static_cast<int>(v); break;
    }
  }

  // Query helpers.
  bool hasActiveSlider() const noexcept {
    return activeSlider_ >= 0;
  }
};

// Singleton
inline TaskbarTabM3State& taskbarM3() {
  static TaskbarTabM3State s;
  return s;
}

inline bool taskbarM3_is_appearance_child_tab() {
  return taskbarM3().activeChildTab_ == TaskbarTabM3State::kTabAppearance;
}

inline bool taskbarM3_is_widgets_child_tab() {
  return taskbarM3().activeChildTab_ == TaskbarTabM3State::kTabWidgets;
}

} // namespace m3::detail
