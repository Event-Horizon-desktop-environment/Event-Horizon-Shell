#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "m3/core/primitives/box.hpp"
#include <cairo/cairo.h>

namespace eh::shell::dock::popup::weather {
namespace {

constexpr double kPad          = 20.0;
constexpr double kGapSm        = 6.0;
constexpr double kGapMd        = 14.0;
constexpr double kGapLg        = 20.0;
constexpr double kCornerRadius = 20.0;

constexpr double kTempSz       = 52.0;
constexpr double kUnitSz       = 11.5;
constexpr double kCondSz       = 12.0;
constexpr double kCitySz       = 26.0;
constexpr double kStatValueSz  = 13.0;
constexpr double kStatLabelSz  = 10.0;
constexpr double kSectionSz    = 12.5;
constexpr double kCardDaySz    = 11.5;
constexpr double kCardTempSz   = 10.5;
constexpr double kCardIconSz   = 22.0;

constexpr double kStatPillGap  = 8.0;
constexpr double kStatPillH    = 62.0;
constexpr double kStatIconSz   = 16.0;

constexpr double kCardGap      = 7.0;
constexpr double kCardH        = 92.0;

constexpr double kHeroIconSz   = 52.0;
constexpr double kHeroIconGap  = 14.0;
constexpr double kForeDayY     = 15.0;
constexpr double kForeIconFrac = 0.5;
constexpr double kForeTempBot  = 9.0;

constexpr double kShadOffX     = 2.0;
constexpr double kShadOffY     = 4.0;
constexpr double kShadAlpha    = 0.28;

struct CloseBtnGeom { double x, y, w, h; };
CloseBtnGeom close_btn(double W) {
  return {W - kPad - 24.0, kPad, 24.0, 24.0};
}

void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  const double rad = std::min({r, w * 0.5, h * 0.5});
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - rad, y + rad,     rad, -M_PI_2,      0);
  cairo_arc(cr, x + w - rad, y + h - rad, rad,       0, M_PI_2);
  cairo_arc(cr, x + rad,     y + h - rad, rad,  M_PI_2,   M_PI);
  cairo_arc(cr, x + rad,     y + rad,     rad,     M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

}

template<typename A>
inline void dock_weather_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  const double W = static_cast<double>(kWeatherPopupW);
  const double H = static_cast<double>(kWeatherPopupH);

  const double contentW  = W - kPad * 2.0;
  const double statPillW = (contentW - 3.0 * kStatPillGap) / 4.0;
  const double cardW     = (contentW - 4.0 * kCardGap) / 5.0;

  const double shellOv = static_cast<double>(
      eh::config::overlay_surface_alpha_scale(sc, eh::config::OverlaySurfaceAlphaKind::Weather));
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const auto& ws = eh::widgets::control_center_weather_state(sc, app.weatherInstanceId);
  const bool f = ws.fahrenheit;

  cairo_save(cr);
  rounded_rect(cr, kShadOffX, kShadOffY, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, 0, 0, 0, kShadAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  {
    m3::Box box;
    box.setColor(static_cast<float>(mc.dockFillR * 0.35), static_cast<float>(mc.dockFillG * 0.35),
                 static_cast<float>(mc.dockFillB * 0.35), static_cast<float>(0.78 * shellOv));
    box.setRadius(static_cast<float>(kCornerRadius));
    box.setGeometry(0, 0, static_cast<float>(W), static_cast<float>(H));
    box.setGlassy(true);
    box.paint(cr);
  }

  rounded_rect(cr, 0.5, 0.5, W - 1.0, H - 1.0, kCornerRadius);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const CloseBtnGeom cb = close_btn(W);
  const double px = app.pointerX, py = app.pointerY;
  const bool hc = px >= cb.x && px < cb.x + cb.w && py >= cb.y && py < cb.y + cb.h;
  {
    m3::Box cbBox;
    cbBox.setColor(0.3f, 0.3f, 0.35f, hc ? 0.75f : 0.45f);
    cbBox.setRadius(7.0f);
    cbBox.setGeometry(static_cast<float>(cb.x), static_cast<float>(cb.y),
                      static_cast<float>(cb.w), static_cast<float>(cb.h));
    cbBox.setGlassy(true);
    cbBox.paint(cr);
  }
  eh::shell::draw_material_glyph(cr, cb.x + cb.w * 0.5, cb.y + cb.h * 0.5,
                                 14, "close", mc.textR, mc.textG, mc.textB, hc ? 1.0 : 0.85);

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
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, kTempSz);
  cairo_text_extents_t teT;
  cairo_text_extents(cr, tempBuf, &teT);
  const double tempX = kPad + kHeroIconSz + kHeroIconGap;
  cairo_move_to(cr, tempX, tempBaseline);
  cairo_show_text(cr, tempBuf);

  const char* unitLabel = f ? "Fahrenheit" : "Celsius";
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.68);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, kUnitSz);
  cairo_move_to(cr, tempX, unitBaseline);
  cairo_show_text(cr, unitLabel);

  const char* condition = ws.available ? ws.condition.c_str() : ws.status_text.c_str();
  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.72);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
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
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, kCitySz);
    cairo_text_extents_t teCity;
    cairo_text_extents(cr, locCity.c_str(), &teCity);
    const double cityX    = tempX + teT.x_advance + kHeroIconGap;
    const double maxCityW = W - kPad - cityX;
    if (teCity.x_advance <= maxCityW) {
      cairo_move_to(cr, cityX, heroIconCenterY + kCitySz * 0.35);
      cairo_show_text(cr, locCity.c_str());
    } else {
      cairo_set_font_size(cr, 20.0);
      cairo_move_to(cr, cityX, heroIconCenterY + 20.0 * 0.35);
      cairo_show_text(cr, locCity.c_str());
    }
  }

  const double heroBot = condBaseline + 8.0;

  const double statsY = heroBot + kGapLg;

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

    {
      m3::Box pill;
      pill.setColor(static_cast<float>(mc.drawerDimR), static_cast<float>(mc.drawerDimG),
                    static_cast<float>(mc.drawerDimB), 0.55f);
      pill.setRadius(10.0f);
      pill.setGeometry(static_cast<float>(sx), static_cast<float>(sy),
                       static_cast<float>(statPillW), static_cast<float>(kStatPillH));
      pill.setGlassy(true);
      pill.paint(cr);
    }

    eh::shell::draw_material_glyph(cr, sx + statPillW * 0.5, sy + 16.0, kStatIconSz,
                                   st[i].glyph, mc.accentR, mc.accentG, mc.accentB, 0.85);

    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.68);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, kStatLabelSz);
    cairo_text_extents_t teSL;
    cairo_text_extents(cr, st[i].label, &teSL);
    cairo_move_to(cr, sx + (statPillW - teSL.x_advance) * 0.5, sy + 33.0);
    cairo_show_text(cr, st[i].label);

    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.95);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, kStatValueSz);
    cairo_text_extents_t teSV;
    cairo_text_extents(cr, st[i].value, &teSV);
    cairo_move_to(cr, sx + (statPillW - teSV.x_advance) * 0.5, sy + kStatPillH - 10.0);
    cairo_show_text(cr, st[i].value);
  }

  const double foreTitleBaseline = statsY + kStatPillH + kGapLg + kSectionSz;

  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.80);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, kSectionSz);
  cairo_move_to(cr, kPad, foreTitleBaseline);
  cairo_show_text(cr, "5-Day Forecast");

  const double cardTop = foreTitleBaseline + kGapMd;
  const int    nDays   = std::min(5, static_cast<int>(ws.forecast.size()));

  for (int i = 0; i < nDays; ++i) {
    const auto& d  = ws.forecast[static_cast<size_t>(i)];
    const double cx = kPad + static_cast<double>(i) * (cardW + kCardGap);
    const bool today = (i == 0);

    {
      m3::Box card;
      if (today)
        card.setColor(static_cast<float>(mc.accentR * 0.35), static_cast<float>(mc.accentG * 0.28),
                      static_cast<float>(mc.accentB * 0.18), 0.75f);
      else
        card.setColor(static_cast<float>(mc.drawerDimR), static_cast<float>(mc.drawerDimG),
                      static_cast<float>(mc.drawerDimB), 0.50f);
      card.setRadius(10.0f);
      card.setGeometry(static_cast<float>(cx), static_cast<float>(cardTop),
                       static_cast<float>(cardW), static_cast<float>(kCardH));
      card.setGlassy(true);
      card.paint(cr);
    }

    if (today) {
      rounded_rect(cr, cx, cardTop, cardW, kCardH, 10);
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.30);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    if (today)
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 1.0);
    else
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.88);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
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
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, kCardTempSz);
    char flBuf[20];
    std::snprintf(flBuf, sizeof(flBuf), "%d\xC2\xB0/%d\xC2\xB0", d.hi, d.lo);
    cairo_text_extents_t teF;
    cairo_text_extents(cr, flBuf, &teF);
    cairo_move_to(cr, cx + (cardW - teF.x_advance) * 0.5, cardTop + kCardH - kForeTempBot);
    cairo_show_text(cr, flBuf);
  }
}

}
