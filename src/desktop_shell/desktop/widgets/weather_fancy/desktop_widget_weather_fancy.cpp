#include "desktop_shell/desktop/widgets/weather_fancy/desktop_widget_weather_fancy.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/dock/core/dock_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"

namespace eh::shell::desktop {
namespace {

using eh::shell::shared::rounded_rect;

constexpr double kForeIconFrac = 0.48;

}

DesktopWeatherFancyWidget::DesktopWeatherFancyWidget(std::string widgetId)
    : m_widgetId(std::move(widgetId)) {}

DesktopWeatherFancyWidget::~DesktopWeatherFancyWidget() = default;

void DesktopWeatherFancyWidget::create() {
   
  auto& sc = eh::config::shell_config_snapshot();
  (void)sc;
}

void DesktopWeatherFancyWidget::paint(cairo_t* cr, const eh::config::ShellConfig& sc) {
   
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double W = static_cast<double>(intrinsicWidth());
  const double H = static_cast<double>(intrinsicHeight());

  const double kPad          = 16.0 * us;
  const double kGapSm        = 4.0 * us;
  const double kGapMd        = 10.0 * us;
  const double kGapLg        = 16.0 * us;
  const double kCornerRadius = 16.0 * us;

  const double kTempSz       = 42.0 * us;
  const double kUnitSz       = 10.0 * us;
  const double kCondSz       = 10.0 * us;
  const double kCitySz       = 18.0 * us;
  const double kStatValueSz  = 11.0 * us;
  const double kStatLabelSz  = 8.5 * us;
  const double kCardDaySz    = 10.0 * us;
  const double kCardTempSz   = 9.0 * us;
  const double kCardIconSz   = 18.0 * us;

  const double kStatPillGap  = 6.0 * us;
  const double kStatPillH    = 52.0 * us;
  const double kStatIconSz   = 14.0 * us;

  const double kCardGap      = 5.0 * us;
  const double kCardH        = 80.0 * us;

  const double kHeroIconSz   = 40.0 * us;
  const double kHeroIconGap  = 10.0 * us;

  const double kForeDayY     = 12.0 * us;
  const double kForeTempBot  = 7.0 * us;

  const double contentW  = W - kPad * 2.0;
  const double statPillW = (contentW - 3.0 * kStatPillGap) / 4.0;
  const double cardW     = (contentW - 4.0 * kCardGap) / 5.0;

  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const auto& ws = eh::shell::dock::control_center::control_center_weather_state(sc, m_widgetId);
  const bool f = ws.fahrenheit;

  cairo_save(cr);

  eh::shell::shared::paint_glass_card(cr, 0, 0, W, H, kCornerRadius, mc,
                                      sc.appearance.overlayOpacityWidgetCard);

  // Hero section
  const double heroIconCenterY = kPad + kHeroIconSz * 0.5;

  const char* heroIcon = (ws.available && !ws.icon.empty()) ? ws.icon.c_str() : "cloud";
  eh::shell::draw_material_glyph(cr, kPad + kHeroIconSz * 0.5, heroIconCenterY,
                                 kHeroIconSz, heroIcon,
                                 0.82, 0.75, 0.62, 0.88);

  const double tempBaseline = heroIconCenterY + kTempSz * 0.35;
  const double unitBaseline = tempBaseline + kGapSm + kUnitSz;
  const double condBaseline = unitBaseline + kGapSm + kCondSz;

  char tempBuf[16];
  std::snprintf(tempBuf, sizeof(tempBuf), "%d\xC2\xB0", ws.available ? ws.temp : 0);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.98);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, kTempSz);
  cairo_text_extents_t teT;
  cairo_text_extents(cr, tempBuf, &teT);
  const double tempX = kPad + kHeroIconSz + kHeroIconGap;
  cairo_move_to(cr, tempX, tempBaseline);
  cairo_show_text(cr, tempBuf);

  const char* unitLabel = f ? "Fahrenheit" : "Celsius";
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.68);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, kUnitSz);
  cairo_move_to(cr, tempX, unitBaseline);
  cairo_show_text(cr, unitLabel);

  const char* condition = ws.available ? ws.condition.c_str() : ws.status_text.c_str();
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.72);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, kCondSz);
  cairo_move_to(cr, tempX, condBaseline);
  cairo_show_text(cr, condition);

  if (!ws.location.empty()) {
    const std::string& locFull = ws.location;
    const auto commaPos = locFull.find(',');
    const std::string locCity = (commaPos != std::string::npos)
                                ? locFull.substr(0, commaPos)
                                : locFull;

    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.75);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, kCitySz);
    cairo_text_extents_t teCity;
    cairo_text_extents(cr, locCity.c_str(), &teCity);
    const double cityX    = tempX + teT.x_advance + kHeroIconGap;
    const double maxCityW = W - kPad - cityX;
    if (teCity.x_advance <= maxCityW) {
      cairo_move_to(cr, cityX, heroIconCenterY + kCitySz * 0.35);
      cairo_show_text(cr, locCity.c_str());
    } else {
      cairo_set_font_size(cr, 16.0 * us);
      cairo_move_to(cr, cityX, heroIconCenterY + 16.0 * us * 0.35);
      cairo_show_text(cr, locCity.c_str());
    }
  }

  const double heroBot = condBaseline + 6.0 * us;
  const double statsY = heroBot + kGapLg;

  // Stat pills
  struct Stat { const char* glyph; const char* label; const char* value; };
  Stat st[4];
  char feelsV[16], humidV[16], windV[16], visV[16];
  if (ws.available) {
    std::snprintf(feelsV, sizeof(feelsV), "%d\xC2\xB0", ws.feels_like);
    std::snprintf(humidV, sizeof(humidV), "%d%%", ws.humidity_pct);
    std::snprintf(windV,  sizeof(windV),  "%d km/h", ws.wind_kmh);
    if (ws.visibility_m >= 1000)
      std::snprintf(visV, sizeof(visV), "%.0f km", ws.visibility_m / 1000.0);
    else
      std::snprintf(visV, sizeof(visV), "%d m", ws.visibility_m);
  } else {
    std::snprintf(feelsV, sizeof(feelsV), "--");
    std::snprintf(humidV, sizeof(humidV), "--");
    std::snprintf(windV,  sizeof(windV),  "--");
    std::snprintf(visV,   sizeof(visV),   "--");
  }
  st[0] = {"thermostat",          "Feels Like", feelsV};
  st[1] = {"humidity_percentage", "Humidity",   humidV};
  st[2] = {"air",                 "Wind",       windV};
  st[3] = {"visibility",          "Visibility", visV};

  for (int i = 0; i < 4; ++i) {
    const double sx = kPad + static_cast<double>(i) * (statPillW + kStatPillGap);
    const double sy = statsY;

    rounded_rect(cr, sx, sy, statPillW, kStatPillH, 8.0 * us);
    cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.55);
    cairo_fill(cr);

    eh::shell::draw_material_glyph(cr, sx + statPillW * 0.5, sy + 13.0 * us, kStatIconSz,
                                   st[i].glyph, mc.accentR, mc.accentG, mc.accentB, 0.85);

    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.68);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, kStatLabelSz);
    cairo_text_extents_t teSL;
    cairo_text_extents(cr, st[i].label, &teSL);
    cairo_move_to(cr, sx + (statPillW - teSL.x_advance) * 0.5, sy + 27.0 * us);
    cairo_show_text(cr, st[i].label);

    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.95);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, kStatValueSz);
    cairo_text_extents_t teSV;
    cairo_text_extents(cr, st[i].value, &teSV);
    cairo_move_to(cr, sx + (statPillW - teSV.x_advance) * 0.5, sy + kStatPillH - 8.0 * us);
    cairo_show_text(cr, st[i].value);
  }

  // 5-day forecast
  const double foreTitleBaseline = statsY + kStatPillH + kGapLg + 12.0 * us;

  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.80);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 11.0 * us);
  cairo_move_to(cr, kPad, foreTitleBaseline);
  cairo_show_text(cr, "5-Day Forecast");

  const double cardTop = foreTitleBaseline + kGapMd;
  const int    nDays   = std::min(5, static_cast<int>(ws.forecast.size()));

  for (int i = 0; i < nDays; ++i) {
    const auto& d  = ws.forecast[static_cast<size_t>(i)];
    const double cx = kPad + static_cast<double>(i) * (cardW + kCardGap);
    const bool today = (i == 0);

    rounded_rect(cr, cx, cardTop, cardW, kCardH, 8.0 * us);
    if (today)
      cairo_set_source_rgba(cr, mc.accentR * 0.35, mc.accentG * 0.28, mc.accentB * 0.18, 0.75);
    else
      cairo_set_source_rgba(cr, mc.drawerDimR, mc.drawerDimG, mc.drawerDimB, 0.50);
    cairo_fill(cr);

    if (today) {
      rounded_rect(cr, cx, cardTop, cardW, kCardH, 8.0 * us);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.30);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    if (today)
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 1.0);
    else
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.88);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, kCardDaySz);
    cairo_text_extents_t teD;
    cairo_text_extents(cr, d.day.c_str(), &teD);
    cairo_move_to(cr, cx + (cardW - teD.x_advance) * 0.5, cardTop + kForeDayY);
    cairo_show_text(cr, d.day.c_str());

    eh::shell::draw_material_glyph(cr, cx + cardW * 0.5, cardTop + kCardH * kForeIconFrac,
                                   kCardIconSz, d.icon.c_str(),
                                   mc.accentR, mc.accentG, mc.accentB,
                                   today ? 0.95 : 0.82);

    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, today ? 0.90 : 0.78);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, kCardTempSz);
    char flBuf[20];
    std::snprintf(flBuf, sizeof(flBuf), "%d\xC2\xB0/%d\xC2\xB0", d.hi, d.lo);
    cairo_text_extents_t teF;
    cairo_text_extents(cr, flBuf, &teF);
    cairo_move_to(cr, cx + (cardW - teF.x_advance) * 0.5, cardTop + kCardH - kForeTempBot);
    cairo_show_text(cr, flBuf);
  }

  cairo_restore(cr);
}

}
