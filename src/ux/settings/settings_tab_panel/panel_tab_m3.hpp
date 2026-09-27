#pragma once

// Panel settings tab — brand-new M3 UI with three child tabs
// (Settings / Widgets / Appearance), mirroring the taskbar tab structure
// but with panel-specific rows, ranges and defaults. All geometry,
// state and copy is original to the panel.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

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
#include "ux/settings/utils/display/settings_display_dropdown.hpp"

#include "ux/settings/settings_tab_panel/settings_tab_panel.hpp"
#include "desktop_shell/common/log/debug_log.hpp"

extern void draw(App& app);
extern const char* const kPanelWidthModeLabels[];

namespace m3::detail {

struct PanelTabM3State {
  static constexpr int kPanelVisToggleRows = 6;
  static constexpr int kPanelVisRowsWithDisplay = 7;
  static constexpr int kPanelVisCardH = 52 + kDockVisRowPitch * kPanelVisRowsWithDisplay + 24;

  Toggle showPanel;
  Toggle positionTop;
  Toggle autoHide;
  Toggle tooltips;
  Toggle embeddedWidgets;
  Toggle widgetsEnabledAsToggle;

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

  Toggle panelBorder;

  int activeChildTab_ = 0;
  static constexpr int kChildTabGap = 0;
  enum { kTabSettings = 0, kTabWidgets = 1, kTabAppearance = 2 };

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
    accentR_ = a_r;
    accentG_ = a_g;
    accentB_ = a_b;
    textR_ = t_r;
    textG_ = t_g;
    textB_ = t_b;
    surfaceR_ = s_r;
    surfaceG_ = s_g;
    surfaceB_ = s_b;
    outlineR_ = o_r;
    outlineG_ = o_g;
    outlineB_ = o_b;
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
    applyAll(showPanel);
    applyAll(positionTop);
    applyAll(autoHide);
    applyAll(tooltips);
    applyAll(embeddedWidgets);
    applyAll(widgetsEnabledAsToggle);
    applyBase(heightSlider);
    applyBase(radiusSlider);
    applyBase(opacitySlider);
    applyBase(iconSizeSlider);
    applyBase(iconSpacingSlider);
    applyBase(floatingGapSlider);
    applyBase(edgeGapSlider);
    applyBase(exclusiveZoneSlider);
    applyBase(scaleSlider);
    applyBase(borderSizeSlider);
    applyAll(panelBorder);
  }

  void updateHover(const App& app) {
    const float scroll = static_cast<float>(settings_scroll_px(app));
    const float px = static_cast<float>(app.pointerX);
    const float py = static_cast<float>(app.pointerY) + scroll;
    auto setH = [&](auto& w) { w.setHovered(w.containsPoint(px, py)); };
    setH(showPanel);
    setH(positionTop);
    setH(autoHide);
    setH(tooltips);
    setH(embeddedWidgets);
    setH(widgetsEnabledAsToggle);
    setH(panelBorder);
    Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider, &iconSizeSlider, &iconSpacingSlider,
                         &floatingGapSlider, &edgeGapSlider, &exclusiveZoneSlider, &scaleSlider, &borderSizeSlider};
    for (auto* s : sliders) setH(*s);
  }

  static void paintChildTabBar(cairo_t* cr, int contentX, int contentW, float textR, float textG, float textB,
                               int activeTab) {
    constexpr int kTabW = 130;
    const int barX = contentX + 8;
    const int barY = kContentTop;
    const int barW = contentW - 16;
    {
      m3::Box bg;
      bg.setColor(textR, textG, textB, 0.04f);
      bg.setRadius(8.0f);
      bg.setGeometry(static_cast<float>(barX), static_cast<float>(barY), static_cast<float>(barW),
                     static_cast<float>(kDockChildTabH + 6));
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
        selBg.setGeometry(static_cast<float>(tx), static_cast<float>(tabY), static_cast<float>(kTabW),
                          static_cast<float>(tabH));
        selBg.setGlassy(true);
        selBg.paint(cr);
      }
      m3::Label lbl;
      lbl.setText(kChildLabels[i]);
      lbl.setFontSize(13.0f);
      lbl.setFontWeight(sel ? 600 : 400);
      lbl.setColor(textR, textG, textB, sel ? 0.90f : 0.55f);
      float lw, lh;
      lbl.measureExtents(lw, lh);
      lbl.paintAt(cr, static_cast<float>(tx) + (static_cast<float>(kTabW) - lw) * 0.5f,
                  static_cast<float>(tabY) + (static_cast<float>(tabH) - lh) * 0.5f);
    }
  }

  int hitChildTab(float px, float py, int contentX, int) const {
    constexpr int kTabW = 130;
    const int barX = contentX + 8;
    const int barY = kContentTop + 3;
    for (int i = 0; i < 3; ++i) {
      const int tx = barX + 4 + i * (kTabW + kChildTabGap);
      if (px >= tx && px < tx + kTabW && py >= barY && py < barY + kDockChildTabH) return i;
    }
    return -1;
  }

  void widthModeComboGeom(int contentX, int contentW, int& cbx, int& cby, int& cbw, int& cbh) const {
    const int kVisCardTop = kContentTop + kDockChildTabH + 12;
    const int rowY = kVisCardTop + 52 + 1 * kDockVisRowPitch;
    const int cardX = contentX + 8;
    const int cardW = contentW - 16;
    cbx = static_cast<int>(cardX + cardW - kCardPad - static_cast<double>(kSettingsComboW));
    cby = rowY + (kDockVisRowPitch - kSettingsComboH) / 2;
    cbw = kSettingsComboW;
    cbh = kSettingsComboH;
  }

  void paint(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, int) {
    syncColours(app);
    applyColours();
    paintChildTabBar(cr, contentX, contentW, textR_, textG_, textB_, activeChildTab_);
    if (activeChildTab_ == kTabWidgets) {
      paintWidgets(app, cr, contentX, contentW, glassOv);
      return;
    }
    if (activeChildTab_ == kTabSettings) {
      paintSettings(app, cr, contentX, contentW, glassOv);
      return;
    }
    if (activeChildTab_ == kTabAppearance) {
      paintAppearance(app, cr, contentX, contentW, glassOv);
      return;
    }
  }

  void paintSettings(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
    updateHover(app);
    const float cardX = static_cast<float>(contentX + 8);
    const float cardW = static_cast<float>(contentW - 16);
    const int kVisCardTop = kContentTop + kDockChildTabH + 12;
    auto visBandTop = [&](int row) { return kVisCardTop + 52 + row * kDockVisRowPitch; };
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(kVisCardTop),
                  static_cast<double>(cardW), static_cast<double>(kPanelVisCardH), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(kVisCardTop + 21), "Panel");
    settings_show_text(cr, cardX + kCardPad, kVisCardTop + 38, "Control how your panel appears and behaves", 11.f,
                       400, textR_, textG_, textB_, 0.46f);

    auto paintRow = [&](int row, const char* title, const char* sub, Toggle& tg, bool on) {
      const int bandTop = visBandTop(row);
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
      tg.setSize(Toggle::Size::L);
      tg.setGeometry(tgX, tgY, tgW, tgH);
      tg.setOn(on);
      tg.setEnabled(true);
      tg.setAccentColor(accentR_, accentG_, accentB_);
      settings_show_text(cr, cardX + kCardPad, titleY, title, 14.f, 500, textR_, textG_, textB_, 0.93f);
      settings_show_text(cr, cardX + kCardPad, titleY + 17, sub, 11.f, 400, textR_, textG_, textB_, 0.46f);
      tg.paint(cr, 0);
    };

    paintRow(0, "Show panel", "Render the top/bottom indicator bar.", showPanel, app.settings.panelEnabled);
    // Row 1: width mode combo.
    {
      const int bandTop = visBandTop(1);
      cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.15);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, static_cast<double>(cardX + kCardPad + 8), bandTop);
      cairo_line_to(cr, static_cast<double>(cardX + cardW - kCardPad - 8), bandTop);
      cairo_stroke(cr);
      int cbx, cby, cbw, cbh;
      widthModeComboGeom(contentX, contentW, cbx, cby, cbw, cbh);
      settings_show_text(cr, cardX + kCardPad, bandTop + 36, "Width mode", 14.f, 500, textR_, textG_, textB_, 0.93f);
      settings_show_text(cr, cardX + kCardPad, bandTop + 53, "Floating, edge-to-edge, or content fill.", 11.f, 400,
                         textR_, textG_, textB_, 0.46f);
      settings_paint_combo_closed(app, cr, cbx, cby, cbw, cbh, glassOv,
                                  kPanelWidthModeLabels[std::clamp(app.settings.panelWidthMode, 0, 2)],
                                  app.panelWidthModeDropdownOpen);
    }
    paintRow(2, "Position at top", "Place the panel at the top edge (off = bottom).", positionTop,
             app.settings.panelPositionTop);
    paintRow(3, "Auto-hide", "Hide the panel until the edge is hovered.", autoHide, app.settings.panelAutoHide);
    paintRow(4, "Tooltips", "Show tooltips when hovering panel widgets.", tooltips,
             app.settings.panelTooltipsEnabled);
    paintRow(5, "Embedded widgets", "Show widgets inside the panel strip.", embeddedWidgets,
             app.settings.panelWidgetsEnabled);
    // Row 6: Display (output picker).
    {
      const int bandTop = visBandTop(6);
      cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 0.15);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, static_cast<double>(cardX + kCardPad + 8), bandTop);
      cairo_line_to(cr, static_cast<double>(cardX + cardW - kCardPad - 8), bandTop);
      cairo_stroke(cr);
      settings_show_text(cr, cardX + kCardPad, bandTop + 36, "Display", 14.f, 500, textR_, textG_, textB_, 0.93f);
      settings_show_text(cr, cardX + kCardPad, bandTop + 53, "Auto = one monitor, All = every display.", 11.f, 400,
                         textR_, textG_, textB_, 0.46f);
      eh::settings::display::sync_dropdown(app, app.panelDisplayDd, app.settings.panelOutputName, contentX,
                                           contentW, bandTop);
      app.panelDisplayDd.paint_trigger(app, cr, glassOv, settings_scroll_px(app));
    }
  }

  void paintWidgets(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
    updateHover(app);
    const float cardX = static_cast<float>(contentX + 8);
    const float cardW = static_cast<float>(contentW - 16);
    const int widgetsTop = kContentTop + kDockChildTabH + 12;
    constexpr int kWidgetToggleCardH = kDockWidgetToggleCardH;
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(widgetsTop), static_cast<double>(cardW),
                  static_cast<double>(kWidgetToggleCardH), glassOv);
    {
      constexpr float tgH = 26.0f;
      constexpr float tgW = 32.0f;
      const float tgX = cardX + cardW - kCardPad - tgW;
      const float tgY = static_cast<float>(widgetsTop) + (kWidgetToggleCardH - tgH) * 0.5f;
      widgetsEnabledAsToggle.setSize(Toggle::Size::M);
      widgetsEnabledAsToggle.setGeometry(tgX, tgY, tgW, tgH);
      widgetsEnabledAsToggle.setOn(app.settings.panelWidgetsEnabled);
      widgetsEnabledAsToggle.setEnabled(true);
      settings_show_text(cr, cardX + kCardPad, widgetsTop + 34, "Embedded panel widgets", 15.f, 400, textR_, textG_,
                         textB_, 0.93f);
      settings_show_text(cr, cardX + kCardPad, widgetsTop + 51, "Show widgets inside the panel strip.", 11.f, 400,
                         textR_, textG_, textB_, 0.46f);
      widgetsEnabledAsToggle.paint(cr, 0);
    }
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
      static const char* kColTitles[] = {"LEFT", "CENTER", "RIGHT"};
      const double listY = colTop + 26.0;
      constexpr double kCardH = 54.0;
      constexpr double kCardGapV = 8.0;
      for (int s = 0; s < 3; ++s) {
        const double colX = cX + static_cast<double>(s) * (colW + colGap);
        // Panel tab is activeTab==1, so widgets_for_section_const resolves to panel vectors.
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
          const bool slotOn = app.settings.panelWidgetSlotsDisabled.find(wid) == app.settings.panelWidgetSlotsDisabled.end();
          {
            m3::Box bg;
            bg.setColor(s_r, s_g, s_b, 0.90f);
            bg.setRadius(12.0f);
            bg.setGeometry(static_cast<float>(colX), static_cast<float>(cy), static_cast<float>(colW),
                           static_cast<float>(kCardH));
            bg.setGlassy(true);
            bg.paint(cr);
          }
          settings_draw_drag_handle_row(app, cr, colX + 10.0, cy + kCardH * 0.5, 1.0);
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
            ibox.setGeometry(static_cast<float>(iconX), static_cast<float>(iconY), static_cast<float>(iconS),
                             static_cast<float>(iconS));
            ibox.setGlassy(true);
            ibox.paint(cr);
          }
          material_symbols_draw_glyph(cr, iconX + iconS * 0.5, iconY + iconS * 0.5 + 0.5, 20.0,
                                      dock_widget_material_ligature(wid), t_r, t_g, t_b, 0.95);
          const double nameX = iconX + iconS + 6.0;
          const double nameW = colX + colW - 8.0 - 84.0 - nameX;
          settings_draw_trimmed_text_line(cr, widget_display_title(wid), nameX, cy + 35.0,
                                          static_cast<size_t>(std::max(0.0, nameW)), 1.0, 13.f, 600);
          const double rmX = colX + colW - 8.0 - 26.0;
          const double rmY = cy + (kCardH - 26.0) * 0.5;
          const bool hovRemove = (px >= rmX && px < rmX + 26.0 && py >= rmY && py < rmY + 26.0);
          settings_draw_row_remove_button(app, cr, rmX, rmY, 26.0, hovRemove, 0.9);
          const double tgX = rmX - 8.0 - 42.0;
          const double tgY = cy + (kCardH - 24.0) * 0.5;
          const bool hovToggle = (px >= tgX && px < tgX + 42.0 && py >= tgY && py < tgY + 24.0);
          settings_draw_widget_accent_toggle(app, cr, tgX, tgY, hovToggle, 0.8, slotOn);
        }
        const double addY = listY + static_cast<double>(widgets.size()) * (kCardH + kCardGapV) + kCardGapV;
        const bool hoverAdd = (px >= colX && px < colX + colW && py >= addY && py < addY + 38.0);
        m3::Button addBtn;
        addBtn.setMinSize(0, 0);
        addBtn.setGlyph("add");
        addBtn.setLabel("Add widget");
        addBtn.setGeometry(static_cast<float>(colX), static_cast<float>(addY), static_cast<float>(colW), 38.0f);
        addBtn.setStyle(m3::Button::Style::Outlined);
        addBtn.setSize(m3::Button::Size::XS);
        addBtn.setAccentColor(a_r, a_g, a_b);
        addBtn.setOutlineColor(o_r, o_g, o_b);
        addBtn.setHovered(hoverAdd);
        addBtn.paint(cr);
      }
      return;
    }
  }

  static void appearSliderGeom(int contentX, int contentW, int idx, int& trX, int& trY, int& trW, int& cardX,
                               int& cardY, int& cardW) {
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

  static void drawValuePill(cairo_t* cr, int cx, int cy, int cw, const char* text, float textR, float textG,
                            float textB) {
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
    const int pillY = cy + 28 - static_cast<int>(te.height) / 2 - padY;
    {
      m3::Box bg;
      bg.setColor(textR, textG, textB, 0.08f);
      bg.setRadius(static_cast<float>(pillH / 2));
      bg.setGeometry(static_cast<float>(pillX), static_cast<float>(pillY), static_cast<float>(pillW),
                     static_cast<float>(pillH));
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
    static const char* titles[] = {"Height",        "Corner Radius", "Opacity",      "Icon Size",  "Icon Spacing",
                                   "Panel Border",  "Scale",         "Border Size",  "Panel Margin", "Exclusive Zone"};
    const int nItems = 10;
    int sliderVals[10];
    sliderVals[0] = app.settings.panelHeight;
    sliderVals[1] = app.settings.panelRadius;
    sliderVals[2] = app.settings.panelOpacity;
    sliderVals[3] = app.settings.panelIconSize;
    sliderVals[4] = app.settings.panelIconSpacing;
    sliderVals[5] = app.settings.panelFloatingAmount;
    sliderVals[6] = static_cast<int>(std::lround(std::clamp(app.settings.panelScale, 0.5, 2.0) * 100.0));
    sliderVals[7] = app.settings.panelBorderSize;
    sliderVals[8] = app.settings.panelEdgeGap;
    sliderVals[9] = app.settings.panelExclusiveZoneGap;
    float sliderMin[10] = {20, 0, 0, 8, 0, 0, 50, 1, 0, 0};
    float sliderMax[10] = {120, 50, 100, 96, 50, 50, 150, 12, 25, 100};
    Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider, &iconSizeSlider, &iconSpacingSlider,
                         &floatingGapSlider, &scaleSlider, &borderSizeSlider, &edgeGapSlider, &exclusiveZoneSlider};
    for (int i = 0; i < nItems; ++i) {
      int trX, trY, trW, cardX, cardY, cardW;
      appearSliderGeom(contentX, contentW, i, trX, trY, trW, cardX, cardY, cardW);
      settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cardY), static_cast<double>(cardW),
                    92.0, glassOv);
      settings_show_text(cr, static_cast<double>(cardX + 20), static_cast<double>(cardY + 28), titles[i], 13.f, 500,
                         textR_, textG_, textB_, 0.90f);
      if (i == 5) {
        constexpr float tgH = 24.0f;
        constexpr float tgW = 40.0f;
        const float tgX = static_cast<float>(cardX + cardW - 20 - tgW);
        const float tgY = static_cast<float>(cardY) + (92.0f - tgH) * 0.5f;
        panelBorder.setSize(Toggle::Size::L);
        panelBorder.setGeometry(tgX, tgY, tgW, tgH);
        panelBorder.setOn(app.settings.panelBorder);
        panelBorder.setEnabled(true);
        panelBorder.setAccentColor(accentR_, accentG_, accentB_);
        panelBorder.paint(cr, 0);
      } else {
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
        sliders[i]->setGeometry(static_cast<float>(trX), static_cast<float>(trY), static_cast<float>(trW), 28.0f);
        sliders[i]->setShowValueLabel(true);
        sliders[i]->setValueLabel(valStr);
        sliders[i]->paint(cr, 0);
      }
    }
  }

  bool handlePointerDown(App& app, float px, float py, int contentX, int contentW) {
    const int tabHit = hitChildTab(px, py, contentX, contentW);
    if (tabHit >= 0 && tabHit != activeChildTab_) {
      activeChildTab_ = tabHit;
      activeSlider_ = -1;
      app.panelWidthModeDropdownOpen = false;
      app.panelDisplayDd.close();
      draw(app);
      return true;
    }
    if (activeChildTab_ == kTabSettings) return handleSettingsPointerDown(app, px, py, contentX, contentW);
    if (activeChildTab_ == kTabWidgets) return handleWidgetsPointerDown(app, px, py, contentX, contentW);
    if (activeChildTab_ == kTabAppearance) return handleAppearancePointerDown(px, py, contentX, contentW);
    return false;
  }

  bool handleSettingsPointerDown(App& app, float px, float py, int contentX, int contentW) {
    activeSlider_ = -1;
    // Display-output dropdown: row select + trigger toggle + outside-close
    // on pointer-down (shared helper). Takes precedence over width-mode.
    {
      const int kVisCardTop = kContentTop + kDockChildTabH + 12;
      const int dispBand = kVisCardTop + 52 + 6 * kDockVisRowPitch;
      if (app.panelDisplayDd.open()) {
        if (eh::settings::display::handle_pointer_down(app, app.panelDisplayDd,
                                                       app.settings.panelOutputName,
                                                       contentX, contentW, dispBand)) {
          app.panelWidthModeDropdownOpen = false;
          return true;
        }
      }
    }
    if (app.panelWidthModeDropdownOpen) {
      int cbx, cby, cbw, cbh;
      widthModeComboGeom(contentX, contentW, cbx, cby, cbw, cbh);
      const int ly = cby + cbh + 2;
      const int lh = 3 * kSettingsDdRowH;
      if (px >= cbx && px < cbx + cbw && py >= ly && py < ly + lh) {
        const int rr = static_cast<int>((py - ly) / kSettingsDdRowH);
        if (rr >= 0 && rr <= 2) {
          app.settings.panelWidthMode = rr;
          save_settings(app.settings);
        }
        app.panelWidthModeDropdownOpen = false;
        draw(app);
        return true;
      }
      if (px >= cbx && px < cbx + cbw && py >= cby && py < cby + cbh) {
        app.panelWidthModeDropdownOpen = false;
        draw(app);
        return true;
      }
      app.panelWidthModeDropdownOpen = false;
      draw(app);
      return false;
    }
    {
      int cbx, cby, cbw, cbh;
      widthModeComboGeom(contentX, contentW, cbx, cby, cbw, cbh);
      if (px >= cbx && px < cbx + cbw && py >= cby && py < cby + cbh) {
        app.panelWidthModeDropdownOpen = true;
        app.panelDisplayDd.close();
        draw(app);
        return true;
      }
    }
    // Display-output combo click (open dropdown).
    {
      const int kVisCardTop = kContentTop + kDockChildTabH + 12;
      const int dispBand = kVisCardTop + 52 + 6 * kDockVisRowPitch;
      eh::settings::display::sync_dropdown(app, app.panelDisplayDd, app.settings.panelOutputName, contentX,
                                           contentW, dispBand);
      const int scr = settings_scroll_px(app);
      const int sx = static_cast<int>(app.pointerX);
      const int sy = static_cast<int>(app.pointerY);
      if (app.panelDisplayDd.hit_trigger(sx, sy, scr)) {
        app.panelWidthModeDropdownOpen = false;
        app.panelDisplayDd.open_popup();
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
    if (tryToggle(&showPanel)) return true;
    if (tryToggle(&positionTop)) return true;
    if (tryToggle(&autoHide)) return true;
    if (tryToggle(&tooltips)) return true;
    if (tryToggle(&embeddedWidgets)) return true;
    return false;
  }

  bool handleWidgetsPointerDown(App& app, float px, float py, int contentX, int contentW) {
    if (widgetsEnabledAsToggle.containsPoint(px, py)) {
      widgetsEnabledAsToggle.handlePointerDown(px, py);
      return true;
    }
    // Three-column layout must match paintWidgets().
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
      const double addY = listY + static_cast<double>(widgets.size()) * (kCardH + kCardGapV) + kCardGapV;
      if (px >= colX && px < colX + colW && py >= addY && py < addY + 38.0) {
        app.widgetPickerOpen = true;
        app.widgetPickerForPanel = true;
        app.widgetPickerSection = (s == 0) ? "left" : (s == 1) ? "center" : "right";
        app.widgetPickerFilter.clear();
        app.widgetPickerHoverSlot = -1;
        draw(app);
        return true;
      }
      for (size_t i = 0; i < widgets.size(); ++i) {
        const double cy = listY + static_cast<double>(i) * (kCardH + kCardGapV);
        const double rmX = colX + colW - 8.0 - 26.0;
        const double rmY = cy + (kCardH - 26.0) * 0.5;
        if (px >= rmX && px < rmX + 26.0 && py >= rmY && py < rmY + 26.0) {
          const std::string id = widgets[i];
          widgets.erase(widgets.begin() + static_cast<long>(i));
          app.settings.panelWidgetSlotsDisabled.erase(id);
          app.settings.widgetSlotsDisabled.erase(id);
          save_settings(app.settings);
          draw(app);
          return true;
        }
        const double tgX = rmX - 8.0 - 42.0;
        const double tgY = cy + (kCardH - 24.0) * 0.5;
        if (px >= tgX && px < tgX + 42.0 && py >= tgY && py < tgY + 24.0) {
          const std::string& id = widgets[i];
          auto& ds = app.settings.panelWidgetSlotsDisabled;
          if (ds.find(id) != ds.end()) {
            ds.erase(id);
            app.settings.widgetSlotsDisabled.erase(id);
            app.settings.dockWidgetSlotsDisabled.erase(id);
            app.settings.taskbarWidgetSlotsDisabled.erase(id);
          } else {
            ds.insert(id);
          }
          save_settings(app.settings);
          draw(app);
          return true;
        }
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
          const double rowWinTop = cy - settings_scroll_px(app);
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
    {
      int trX, trY, trW, cardX, cardY, cardW;
      appearSliderGeom(contentX, contentW, 5, trX, trY, trW, cardX, cardY, cardW);
      constexpr float tgH = 24.0f;
      constexpr float tgW = 40.0f;
      const float tgX = static_cast<float>(cardX + cardW - 20 - tgW);
      const float tgY = static_cast<float>(cardY) + (92.0f - tgH) * 0.5f;
      panelBorder.setSize(Toggle::Size::L);
      panelBorder.setGeometry(tgX, tgY, tgW, tgH);
      if (panelBorder.containsPoint(px, py)) {
        panelBorder.handlePointerDown(px, py);
        return true;
      }
    }
    Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider, &iconSizeSlider, &iconSpacingSlider,
                         &floatingGapSlider, &scaleSlider, &borderSizeSlider, &edgeGapSlider, &exclusiveZoneSlider};
    float sliderMin[10] = {20, 0, 0, 8, 0, 0, 50, 1, 0, 0};
    float sliderMax[10] = {120, 50, 100, 96, 50, 50, 150, 12, 25, 100};
    for (int i = 0; i < 10; ++i) {
      int trX, trY, trW, cardX, cardY, cardW;
      appearSliderGeom(contentX, contentW, i, trX, trY, trW, cardX, cardY, cardW);
      sliders[i]->setRange(sliderMin[i], sliderMax[i]);
      sliders[i]->setGeometry(static_cast<float>(trX), static_cast<float>(trY), static_cast<float>(trW), 28.0f);
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
    if (activeChildTab_ == kTabAppearance) {
      if (panelBorder.pressed()) {
        panelBorder.handlePointerUp(px, py);
        app.settings.panelBorder = !app.settings.panelBorder;
        handled = true;
      }
      if (activeSlider_ >= 0 && activeSlider_ < 10) {
        Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider, &iconSizeSlider, &iconSpacingSlider,
                             &floatingGapSlider, &scaleSlider, &borderSizeSlider, &edgeGapSlider,
                             &exclusiveZoneSlider};
        sliders[activeSlider_]->handlePointerUp(px, py);
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
      endToggle(&widgetsEnabledAsToggle, &app.settings.panelWidgetsEnabled);
      endToggle(&embeddedWidgets, &app.settings.panelWidgetsEnabled);
      return handled;
    }
    if (activeChildTab_ == kTabSettings) {
      endToggle(&showPanel, &app.settings.panelEnabled);
      endToggle(&positionTop, &app.settings.panelPositionTop);
      endToggle(&autoHide, &app.settings.panelAutoHide);
      endToggle(&tooltips, &app.settings.panelTooltipsEnabled);
      if (!handled) endToggle(&embeddedWidgets, &app.settings.panelWidgetsEnabled);
    }
    if (activeSlider_ >= 0) {
      activeSlider_ = -1;
      handled = true;
    }
    return handled;
  }

  bool handlePointerMove(float px, float py) {
    if (activeChildTab_ == kTabSettings || activeChildTab_ == kTabWidgets) return false;
    if (activeSlider_ >= 0 && activeSlider_ < 10) {
      Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider, &iconSizeSlider, &iconSpacingSlider,
                           &floatingGapSlider, &scaleSlider, &borderSizeSlider, &edgeGapSlider, &exclusiveZoneSlider};
      sliders[activeSlider_]->handlePointerMove(px, py);
      return true;
    }
    return false;
  }

  void handlePointerLeave() {
    activeSlider_ = -1;
    auto reset = [&](auto& w) { w.handlePointerLeave(); };
    reset(showPanel);
    reset(positionTop);
    reset(autoHide);
    reset(tooltips);
    reset(embeddedWidgets);
    reset(widgetsEnabledAsToggle);
    reset(heightSlider);
    reset(radiusSlider);
    reset(opacitySlider);
    reset(iconSizeSlider);
    reset(iconSpacingSlider);
    reset(floatingGapSlider);
    reset(edgeGapSlider);
    reset(exclusiveZoneSlider);
    reset(scaleSlider);
    reset(borderSizeSlider);
    reset(panelBorder);
  }

  void flushSliderValues(App& app) const {
    if (activeChildTab_ != kTabAppearance) return;
    if (activeSlider_ < 0 || activeSlider_ >= 10) return;
    const float v = [&]() -> float {
      const Slider* sliders[] = {&heightSlider, &radiusSlider, &opacitySlider, &iconSizeSlider, &iconSpacingSlider,
                                 &floatingGapSlider, &scaleSlider, &borderSizeSlider, &edgeGapSlider,
                                 &exclusiveZoneSlider};
      return sliders[activeSlider_]->value();
    }();
    switch (activeSlider_) {
      case 0:
        app.settings.panelHeight = static_cast<int>(v);
        break;
      case 1:
        app.settings.panelRadius = static_cast<int>(v);
        break;
      case 2:
        app.settings.panelOpacity = static_cast<int>(v);
        break;
      case 3:
        app.settings.panelIconSize = static_cast<int>(v);
        break;
      case 4:
        app.settings.panelIconSpacing = static_cast<int>(v);
        break;
      case 5:
        app.settings.panelFloatingAmount = static_cast<int>(v);
        break;
      case 6:
        app.settings.panelScale = static_cast<double>(v) / 100.0;
        break;
      case 7:
        app.settings.panelBorderSize = static_cast<int>(v);
        break;
      case 8:
        app.settings.panelEdgeGap = static_cast<int>(v);
        break;
      case 9:
        app.settings.panelExclusiveZoneGap = static_cast<int>(v);
        break;
    }
  }

  bool hasActiveSlider() const noexcept { return activeSlider_ >= 0; }
};

inline PanelTabM3State& panelM3() {
  static PanelTabM3State s;
  return s;
}

inline bool panelM3_is_appearance_child_tab() { return panelM3().activeChildTab_ == PanelTabM3State::kTabAppearance; }

inline bool panelM3_is_widgets_child_tab() { return panelM3().activeChildTab_ == PanelTabM3State::kTabWidgets; }

inline int panel_display_band_top() {
  const int kVisCardTop = kContentTop + kDockChildTabH + 12;
  return kVisCardTop + 52 + 6 * kDockVisRowPitch;
}

inline void panel_display_dd_sync(App& app, int contentX, int contentW) {
  eh::settings::display::sync_dropdown(app, app.panelDisplayDd, app.settings.panelOutputName, contentX, contentW,
                                       panel_display_band_top());
}

inline bool panel_display_dd_commit_pointer_up(App& app, float px, float py, int contentX, int contentW) {
  auto& m3 = panelM3();
  if (!app.panelDisplayDd.open()) return false;
  if (m3.activeChildTab_ != PanelTabM3State::kTabSettings) {
    app.panelDisplayDd.close();
    return false;
  }
  return eh::settings::display::commit_pointer_up(app, app.panelDisplayDd, app.settings.panelOutputName, contentX,
                                                 contentW, panel_display_band_top(), px, py);
}

}  // namespace m3::detail
