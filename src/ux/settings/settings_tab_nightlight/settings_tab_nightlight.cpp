#include "ux/settings/settings_tab_nightlight/settings_tab_nightlight.hpp"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "wl/color/nightlight.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

// Constants.
static constexpr int kTopMargin = 72;
static constexpr int kCardH = 92;
static constexpr int kGap = 12;
static constexpr int kPad = 20;
static constexpr int kTitleY = 28;
static constexpr int kSliderY = 50;

// Slider drag ID namespace (210-214, avoids dock appearance 200-207)
static constexpr int kDragBase = 210;

// Items.
// 0: Enable night light (toggle)
// 1: Day temperature    (slider, 4000-6500)
// 2: Night temperature  (slider, 2500-4000)
// 3: Start time         (slider, 0-1439 min → HH:MM)
// 4: End time           (slider, 0-1439 min → HH:MM)
// 5: Schedule mode      (combo)
static constexpr int kNumItems = 6;

static const char* item_title(int idx) {
  switch (idx) {
    case 0: return "Enable Night Light";
    case 1: return "Day Temperature";
    case 2: return "Night Temperature";
    case 3: return "Start Time";
    case 4: return "End Time";
    case 5: return "Schedule Mode";
    default: return "";
  }
}

static const char* schedule_label(int mode) {
  switch (mode) {
    case 0: return "Manual";
    case 1: return "Sunset to Sunrise";
    case 2: return "Scheduled";
    default: return "Manual";
  }
}

// Geometry.
static void slider_geom(int contentX, int contentW, int idx,
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
  slider_geom(contentX, contentW, idx, trX, trY, trW, cardX, cardY, cardW);
  (void)cardX; (void)cardY; (void)cardW; (void)trY;

  switch (idx) {
    case 1: app.settings.nightlightDayTemp = slider_value_from_x(px, trX, trW, 4000, 6500); break;
    case 2: app.settings.nightlightNightTemp = slider_value_from_x(px, trX, trW, 2500, 4000); break;
    case 3: app.settings.nightlightScheduleStart = slider_value_from_x(px, trX, trW, 0, 1439); break;
    case 4: app.settings.nightlightScheduleEnd = slider_value_from_x(px, trX, trW, 0, 1439); break;
  }
}

// Paint.
void paint_nightlight_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
  for (int i = 0; i < kNumItems; ++i) {
    int trX, trY, trW, cardX, cardY, cardW;
    slider_geom(contentX, contentW, i, trX, trY, trW, cardX, cardY, cardW);

    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(cardY),
                  static_cast<double>(cardW), static_cast<double>(kCardH), glassOv);

    settings_show_text(cr, static_cast<double>(cardX + kPad), static_cast<double>(cardY + kTitleY),
                       item_title(i), 13.f, 500,
                       static_cast<float>(Theme::TextR), static_cast<float>(Theme::TextG),
                       static_cast<float>(Theme::TextB), 0.90f);

    if (i == 0) {
      // Toggle
      settings_toggle(app, cr, cardX, 0, cardW,
                      static_cast<double>(cardY - 24), static_cast<double>(kCardH),
                      app.settings.nightlightEnabled, glassOv);
    } else if (i == 5) {
      // Schedule mode combo — right edge of card, near title
      const int comboW = 200;
      const int comboH = 28;
      const int cbx = cardX + cardW - comboW - kSpacingXL;
      const int cby = cardY + kTitleY - 4;
      settings_paint_combo_closed(app, cr, cbx, cby, comboW, comboH, glassOv,
                                  schedule_label(app.settings.nightlightSchedule), false);
    } else {
      char valStr[32];
      int sliderVal = 0;
      int vmin = 0, vmax = 100;
      const double dragNorm = (app.sliderDrag == kDragBase + i) ? app.settingsSliderDragNormT : -1.0;

      switch (i) {
        case 1:
          sliderVal = std::clamp(app.settings.nightlightDayTemp, 4000, 6500);
          vmin = 4000; vmax = 6500;
          std::snprintf(valStr, sizeof(valStr), "%d K", sliderVal);
          break;
        case 2:
          sliderVal = std::clamp(app.settings.nightlightNightTemp, 2500, 4000);
          vmin = 2500; vmax = 4000;
          std::snprintf(valStr, sizeof(valStr), "%d K", sliderVal);
          break;
        case 3: {
          const int m = std::clamp(app.settings.nightlightScheduleStart, 0, 1439);
          sliderVal = m;
          vmin = 0; vmax = 1439;
          std::snprintf(valStr, sizeof(valStr), "%02d:%02d", m / 60, m % 60);
          break;
        }
        case 4: {
          const int m = std::clamp(app.settings.nightlightScheduleEnd, 0, 1439);
          sliderVal = m;
          vmin = 0; vmax = 1439;
          std::snprintf(valStr, sizeof(valStr), "%02d:%02d", m / 60, m % 60);
          break;
        }
      }

      draw_value_pill(cr, cardX, cardY + 4, cardW, valStr);
      settings_slider(app, cr, trX, trY, trW, sliderVal, vmin, vmax, 0.0, valStr, false, dragNorm);
    }
  }
}

// Pointer down.
bool nightlight_consume_pointer_down(App& app, int contentX, int contentW) {
  // Toggle (index 0)
  {
    int trX, trY, trW, cardX, cardY, cardW;
    slider_geom(contentX, contentW, 0, trX, trY, trW, cardX, cardY, cardW);
    constexpr int swW = 52, swH = 26;
    const int swX = cardX + cardW - swW - kSpacingXL;
    const int swY = cardY - 24 + (kCardH - swH) / 2;
    if (point_in_rect(app.pointerX, app.pointerY, swX, swY, swW, swH)) {
      const bool next = !app.settings.nightlightEnabled;
      app.settings.nightlightEnabled = next;
      if (app.gammaService_ && app.gammaService_->has_gamma_control()) {
        app.gammaService_->set_enabled(next);
        if (next) app.gammaService_->set_temperature(app.settings.nightlightNightTemp);
      }
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  // Sliders (indices 1-4)
  for (int i = 1; i <= 4; ++i) {
    int trX, trY, trW, cardX, cardY, cardW;
    slider_geom(contentX, contentW, i, trX, trY, trW, cardX, cardY, cardW);
    if (point_in_rect(app.pointerX, app.pointerY, trX - 6, trY - 10, trW + 12, 36)) {
      app.sliderDrag = kDragBase + i;
      update_slider_value(app, contentX, contentW, i, app.pointerX);
      app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
      draw(app);
      return true;
    }
  }

  // Schedule combo (index 5)
  {
    int trX, trY, trW, cardX, cardY, cardW;
    slider_geom(contentX, contentW, 5, trX, trY, trW, cardX, cardY, cardW);
    const int comboW = 200;
    const int comboH = 28;
    const int cbx = cardX + cardW - comboW - kSpacingXL;
    const int cby = cardY + kTitleY - 4;
    if (point_in_rect(app.pointerX, app.pointerY, cbx, cby, comboW, comboH)) {
      app.settings.nightlightSchedule = (app.settings.nightlightSchedule + 1) % 3;
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  return false;
}

// Pointer move.
bool nightlight_consume_pointer_move(App& app, int contentX, int contentW) {
  if (app.sliderDrag >= kDragBase + 1 && app.sliderDrag <= kDragBase + 4) {
    const int i = app.sliderDrag - kDragBase;
    int trX, trY, trW, cardX, cardY, cardW;
    slider_geom(contentX, contentW, i, trX, trY, trW, cardX, cardY, cardW);
    app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
    update_slider_value(app, contentX, contentW, i, app.pointerX);
    return true;
  }
  return false;
}
