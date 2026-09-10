#include "ux/settings/settings_tab_dock_appearance/settings_tab_dock_appearance.hpp"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

// Constants.
static constexpr int kTopMargin = 72;
static constexpr int kCardH = 92;
static constexpr int kGap = 12;
static constexpr int kPad = 20;
static constexpr int kTitleY = 28;
static constexpr int kSliderY = 50;

// Slider geometry helper.
static void dock_app_slider_geom(int contentX, int contentW, int idx,
                                 int& trX, int& trY, int& trW,
                                 int& cardX, int& cardY, int& cardW) {
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

// Slider value apply.
static void update_slider_value(App& app, int contentX, int contentW, int idx, double px) {
  int trX, trY, trW, cardX, cardY, cardW;
  dock_app_slider_geom(contentX, contentW, idx, trX, trY, trW, cardX, cardY, cardW);
  (void)cardX; (void)cardY; (void)cardW; (void)trY;

  switch (idx) {
    case 0: app.settings.dockRadius = slider_value_from_x(px, trX, trW, 0, 50); break;
    case 1: {
      int raw = slider_value_from_x(px, trX, trW, 50, 150);
      app.settings.dockScale = static_cast<double>(raw) / 100.0;
      break;
    }
    case 2: app.settings.dockIconSize = slider_value_from_x(px, trX, trW, 0, 50); break;
    case 3: app.settings.dockIconSpacing = slider_value_from_x(px, trX, trW, -50, 50); break;
    case 4: app.settings.dockOpacity = slider_value_from_x(px, trX, trW, 0, 100); break;
    case 5: break; // toggle, not a slider
    case 6: app.settings.dockBorderSize = slider_value_from_x(px, trX, trW, 0, 12); break;
    case 7: app.settings.dockBorderOpacity = slider_value_from_x(px, trX, trW, 0, 100); break;
    case 10: app.settings.dockBottomGap = slider_value_from_x(px, trX, trW, 0, 25); break;
    case 11: app.settings.dockExclusiveZoneGap = slider_value_from_x(px, trX, trW, 0, 100); break;
  }
}

// Paint.
void paint_dock_appearance_tab(App& app, cairo_t* cr, int contentX, int contentW,
                                double glassOv, double paintPointerYOffset) {
  (void)paintPointerYOffset;

  static const char* titles[] = {
    "Corner Radius", "Dock Scale", "Icon Size",
    "Icon Spacing", "Dock Opacity", "Dock Border",
    "Border Size", "Border Opacity", "Liquid Glass", "Colored Glass",
    "Dock Margin", "Exclusive Zone"
  };
  static const int nItems = sizeof(titles) / sizeof(titles[0]);

  for (int i = 0; i < nItems; ++i) {
    int trX, trY, trW, cardX, cardY, cardW;
    dock_app_slider_geom(contentX, contentW, i, trX, trY, trW, cardX, cardY, cardW);

    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cardY),
                  static_cast<double>(cardW), static_cast<double>(kCardH), glassOv);

    if (i == 5 || i == 8 || i == 9) {
      // toggle card
      settings_show_text(cr, static_cast<double>(cardX + kPad), static_cast<double>(cardY + kTitleY),
                         titles[i], 13.f, 500,
                         static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                         static_cast<float>(Theme::TextB), 0.90f);
      bool toggleState = false;
      if (i == 5) toggleState = app.settings.dockBorderEnabled;
      else if (i == 8) toggleState = app.settings.dockLiquidGlass;
      else if (i == 9) toggleState = app.settings.dockColoredGlass;
      settings_toggle(app, cr, cardX, 0, cardW,
                      static_cast<double>(cardY - 24), static_cast<double>(kCardH),
                      toggleState, glassOv);
    } else {
      settings_show_text(cr, static_cast<double>(cardX + kPad), static_cast<double>(cardY + kTitleY),
                         titles[i], 13.f, 500,
                         static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                         static_cast<float>(Theme::TextB), 0.90f);

      char valStr[32];
      int sliderVal = 0;
      int vmin = 0, vmax = 100;
      const double dragNorm = (app.sliderDrag == 200 + i) ? app.settingsSliderDragNormT : -1.0;

      switch (i) {
        case 0:
          sliderVal = app.settings.dockRadius;
          vmin = 0; vmax = 50;
          std::snprintf(valStr, sizeof(valStr), "%d px", sliderVal);
          break;
        case 1:
          sliderVal = static_cast<int>(std::lround(std::clamp(app.settings.dockScale, 0.5, 2.0) * 100.0));
          vmin = 50; vmax = 150;
          std::snprintf(valStr, sizeof(valStr), "%d %%", sliderVal);
          break;
        case 2:
          sliderVal = app.settings.dockIconSize;
          vmin = 0; vmax = 50;
          std::snprintf(valStr, sizeof(valStr), "%d px", sliderVal);
          break;
        case 3:
          sliderVal = app.settings.dockIconSpacing;
          vmin = -50; vmax = 50;
          std::snprintf(valStr, sizeof(valStr), "%d px", sliderVal);
          break;
        case 4:
          sliderVal = app.settings.dockOpacity;
          vmin = 0; vmax = 100;
          std::snprintf(valStr, sizeof(valStr), "%d %%", sliderVal);
          break;
        case 6:
          sliderVal = app.settings.dockBorderSize;
          vmin = 0; vmax = 12;
          std::snprintf(valStr, sizeof(valStr), "%d px", sliderVal);
          break;
        case 7:
          sliderVal = app.settings.dockBorderOpacity;
          vmin = 0; vmax = 100;
          std::snprintf(valStr, sizeof(valStr), "%d %%", sliderVal);
          break;
        case 10:
          sliderVal = app.settings.dockBottomGap;
          vmin = 0; vmax = 25;
          std::snprintf(valStr, sizeof(valStr), "%d px", sliderVal);
          break;
        case 11:
          sliderVal = app.settings.dockExclusiveZoneGap;
          vmin = 0; vmax = 100;
          std::snprintf(valStr, sizeof(valStr), "%d px", sliderVal);
          break;
      }

      draw_value_pill(cr, cardX, cardY + 4, cardW, valStr);
      settings_slider(app, cr, trX, trY, trW, sliderVal, vmin, vmax, 0.0, valStr, false, dragNorm);
    }
  }
}

// Scroll height.
int dock_appearance_tab_scroll_max_px() {
  const int rows = 6;
  return kTopMargin + rows * (kCardH + kGap) + 20;
}

// Pointer down.
bool dock_appearance_consume_pointer_down(App& app, int contentX, int contentW) {
  const double lyA = app.pointerY + settings_scroll_px(app);
  // toggle hit tests (indices 5, 8, 9)
  {
    auto handle_toggle = [&](int idx, bool& flag) {
      int trX, trY, trW, cardX, cardY, cardW;
      dock_app_slider_geom(contentX, contentW, idx, trX, trY, trW, cardX, cardY, cardW);
      constexpr int swW = 52, swH = 26;
      const int swX = cardX + cardW - swW - kSpacingXL;
      const int swY = cardY - 24 + (kCardH - swH) / 2;
      if (point_in_rect(app.pointerX, lyA, swX, swY, swW, swH)) {
        flag = !flag;
        save_settings(app.settings);
        draw(app);
        return true;
      }
      return false;
    };
    if (handle_toggle(5, app.settings.dockBorderEnabled)) return true;
    if (handle_toggle(8, app.settings.dockLiquidGlass)) return true;
    if (handle_toggle(9, app.settings.dockColoredGlass)) return true;
  }
  for (int i = 0; i < 12; ++i) {
    if (i == 5 || i == 8 || i == 9) continue;
    int trX, trY, trW, cardX, cardY, cardW;
    dock_app_slider_geom(contentX, contentW, i, trX, trY, trW, cardX, cardY, cardW);
    if (point_in_rect(app.pointerX, lyA, trX - 6, trY - 10, trW + 12, 36)) {
      app.sliderDrag = 200 + i;
      update_slider_value(app, contentX, contentW, i, app.pointerX);
      app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
      draw(app);
      return true;
    }
  }
  return false;
}

// Pointer move.
bool dock_appearance_consume_pointer_move(App& app, int contentX, int contentW) {
  if (app.sliderDrag >= 200 && app.sliderDrag < 212) {
    const int i = app.sliderDrag - 200;
    if (i != 5 && i != 8 && i != 9) {
      int trX, trY, trW, cardX, cardY, cardW;
      dock_app_slider_geom(contentX, contentW, i, trX, trY, trW, cardX, cardY, cardW);
      app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
      update_slider_value(app, contentX, contentW, i, app.pointerX);
      return true;
    }
  }
  return false;
}
