#include <cairo/cairo.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "m3/core/primitives/box.hpp"
#include "m3/controls/containers/button.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_time/settings_tab_time.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

static constexpr int kCardTop = kContentTop;
static constexpr int kBodyTop = kCardTop + 52;
static constexpr int kFormatCardH = 52 + 3 * kSliderRowH + kSpacingXL;

static constexpr int kDateCardTop = kCardTop + kFormatCardH + kCardGap;
static constexpr int kDateBodyTop = kDateCardTop + 52;
static constexpr int kDateCardH = 52 + kSliderRowH + kSpacingXL;

static constexpr int kPreviewCardTop = kDateCardTop + kDateCardH + kCardGap;
static constexpr int kPreviewCardH = 52 + 100 + kSpacingXL;

static constexpr int kTimezoneCardTop = kPreviewCardTop + kPreviewCardH + kCardGap;
static constexpr int kTimezoneBodyTop = kTimezoneCardTop + 52;
static constexpr int kTimezoneCardH = 52 + 2 * kSliderRowH + kSpacingXL;

static const char* date_format_label(int mode) {
   
  switch (mode) {
    case 0: return "Weekday + Day (Wed 21)";
    case 1: return "Full Date (Wednesday, May 21, 2026)";
    case 2: return "ISO 8601 (2026-05-21)";
    case 3: return "Custom strftime";
    default: return "Weekday + Day";
  }
}

const char* kDateFormatLabels[] = {
  "Weekday + Day (Wed 21)",
  "Full Date (Wednesday, May 21, 2026)",
  "ISO 8601 (2026-05-21)",
  "Custom strftime"
};
const int kDateFormatCount = 4;

void time_date_format_combo_geom(int contentX, int, int& bx, int& by, int& bw, int& bh) {
  bx = contentX + kCardPad;
  by = kDateBodyTop + 26;
  bw = 280;
  bh = 28;
}

static void paint_time_preview(App& app, cairo_t* cr, int, int cardX, int cardW, int previewCardTop) {
  std::time_t t = std::time(nullptr);
  std::tm local{};
  if (localtime_r(&t, &local) == nullptr) return;

  if (!app.settings.timeTimezone.empty()) {
    // Thread-safe timezone lookup using std::chrono instead of setenv("TZ").
    const auto* tzDb = std::chrono::locate_zone(app.settings.timeTimezone);
    if (tzDb != nullptr) {
      const auto zt = std::chrono::zoned_time(tzDb, std::chrono::system_clock::from_time_t(t));
      const auto lt = zt.get_local_time();
      const auto tp = std::chrono::floor<std::chrono::seconds>(lt);
      const auto dp = std::chrono::floor<std::chrono::days>(tp);
      std::chrono::hh_mm_ss hms{tp - dp};
      std::chrono::year_month_day ymd{dp};
      local.tm_year = static_cast<int>(ymd.year()) - 1900;
      local.tm_mon = static_cast<unsigned>(ymd.month()) - 1;
      local.tm_mday = static_cast<unsigned>(ymd.day());
      local.tm_wday = static_cast<int>(std::chrono::weekday{dp}.c_encoding());
      local.tm_hour = static_cast<int>(hms.hours().count());
      local.tm_min = static_cast<int>(hms.minutes().count());
      local.tm_sec = static_cast<int>(hms.seconds().count());
    }
  }

  char timeBuf[64] = {};
  const int h12 = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
  const char* ampm = local.tm_hour < 12 ? "AM" : "PM";

  if (app.settings.timeUse24h) {
    if (app.settings.timeShowSeconds)
      std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
    else
      std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", local.tm_hour, local.tm_min);
  } else {
    if (app.settings.timeShowSeconds)
      std::snprintf(timeBuf, sizeof(timeBuf), "%d:%02d:%02d %s", h12, local.tm_min, local.tm_sec, ampm);
    else
      std::snprintf(timeBuf, sizeof(timeBuf), "%d:%02d %s", h12, local.tm_min, ampm);
  }

  char dateBuf[128] = {};
  if (app.settings.timeShowDate) {
    switch (app.settings.timeDateFormat) {
      case 0: {
        static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        const char* day = (local.tm_wday >= 0 && local.tm_wday <= 6) ? kDays[local.tm_wday] : "???";
        std::snprintf(dateBuf, sizeof(dateBuf), "%s %d", day, local.tm_mday);
        break;
      }
      case 1:
        std::strftime(dateBuf, sizeof(dateBuf), "%A, %B %d, %Y", &local);
        break;
      case 2:
        std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &local);
        break;
      case 3:
        if (!app.settings.timeCustomFormat.empty())
          std::strftime(dateBuf, sizeof(dateBuf), app.settings.timeCustomFormat.c_str(), &local);
        else
          std::strftime(dateBuf, sizeof(dateBuf), "%a %d", &local);
        break;
    }
  }

  const int previewY = previewCardTop + 60;
  const int previewX = cardX + kCardPad + 20;
  const int previewW = cardW - kCardPad - kCardPad - 40;

  {
    m3::Box box;
    float r, g, b;
    if (app.drawChromeMatugen) {
      r = app.drawChrome.panelFillR;
      g = app.drawChrome.panelFillG;
      b = app.drawChrome.panelFillB;
    } else {
      r = static_cast<float>(Theme::BgR);
      g = static_cast<float>(Theme::BgG);
      b = static_cast<float>(Theme::BgB);
    }
    box.setColor(r, g, b, 0.95f);
    box.setRadius(12.0f);
    box.setGeometry(static_cast<float>(previewX), static_cast<float>(previewY),
                    static_cast<float>(previewW), 80.0f);
    box.paint(cr);
  }
  cairo_round_rect(cr, static_cast<double>(previewX), static_cast<double>(previewY),
                   static_cast<double>(previewW), 80.0, 12.0);
  paint_src_glass_hi(app, cr, 0.08);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  settings_show_text(cr, previewX + 20, previewY + 38, timeBuf, 28, 700, Theme::TextR, Theme::TextG, Theme::TextB, 0.95);

  if (dateBuf[0]) {
    settings_show_text(cr, previewX + 22, previewY + 62, dateBuf, 13, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.55);
  }
}

void paint_time_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;

  // Time Format card.
  settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(kCardTop),
                static_cast<double>(cardW), static_cast<double>(kFormatCardH), glassOv);
  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), kCardTop + 22, "TIME FORMAT");

  int rowY = kBodyTop;
  settings_label(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(rowY),
                 "Use 24-hour format",
                 "Display times in 24-hour format (e.g. 14:30 instead of 2:30 PM)");
  settings_toggle(app, cr, cardX, rowY, cardW,
                   static_cast<double>(rowY - 34), 54.0, app.settings.timeUse24h, 0.0);

  rowY += kSliderRowH;
  settings_label(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(rowY),
                 "Show seconds",
                 "Display seconds in the clock (e.g. 14:30:45)");
  settings_toggle(app, cr, cardX, rowY, cardW,
                   static_cast<double>(rowY - 34), 54.0, app.settings.timeShowSeconds, 0.0);

  rowY += kSliderRowH;
  settings_label(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(rowY),
                 "Show date",
                 "Display the date below the time");
  settings_toggle(app, cr, cardX, rowY, cardW,
                   static_cast<double>(rowY - 34), 54.0, app.settings.timeShowDate, 0.0);

  // Date Format card.
  settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(kDateCardTop),
                static_cast<double>(cardW), static_cast<double>(kDateCardH), glassOv);
  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), kDateCardTop + 22, "DATE FORMAT");

  settings_label(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(kDateBodyTop),
                 "Date display style",
                 "How dates appear alongside the clock");

  int cbx, cby, cbw, cbh;
  time_date_format_combo_geom(contentX, contentW, cbx, cby, cbw, cbh);
  settings_paint_combo_closed(app, cr, cbx, cby, cbw, cbh, glassOv,
                               date_format_label(app.settings.timeDateFormat), false);

  // Custom format hint
  if (app.settings.timeDateFormat == 3) {
    std::string customText = "Custom: ";
    if (app.settings.timeCustomFormat.empty())
      customText += "%a %d";
    else
      customText += app.settings.timeCustomFormat;
    settings_show_text(cr, cbx + cbw + 12, cby + 18, customText.c_str(), 11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.46);
  }

  // Preview card.
  settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(kPreviewCardTop),
                static_cast<double>(cardW), static_cast<double>(kPreviewCardH), glassOv);
  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), kPreviewCardTop + 22, "PREVIEW");

  paint_time_preview(app, cr, contentX, cardX, cardW, kPreviewCardTop);

  // Timezone card.
  settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(kTimezoneCardTop),
                static_cast<double>(cardW), static_cast<double>(kTimezoneCardH), glassOv);
  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), kTimezoneCardTop + 22, "TIMEZONE");

  settings_label(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(kTimezoneBodyTop),
                 "Override timezone",
                 "Leave empty to use the system timezone.");

  // Show current system timezone
  {
    const char* sysTz = nullptr;
    if (!app.settings.timeTimezone.empty())
      sysTz = app.settings.timeTimezone.c_str();
    else {
      const char* envTz = std::getenv("TZ");
      if (envTz && envTz[0])
        sysTz = envTz;
      else {
        const std::time_t now = std::time(nullptr);
        auto* rp = std::localtime(&now);
        if (rp) {
          static char tzName[64];
          std::strftime(tzName, sizeof(tzName), "%Z", rp);
          sysTz = tzName;
        }
      }
    }

    settings_show_text(cr, contentX + kCardPad, kTimezoneBodyTop + 44, sysTz ? sysTz : "UTC", 14, 700, Theme::AccR, Theme::AccG, Theme::AccB, 0.90);
  }

  // Clear TZ button
  if (!app.settings.timeTimezone.empty()) {
    const int btnY = kTimezoneBodyTop + kSliderRowH + 4;
    const int btnX = contentX + kCardPad;
    const int btnW = 160;
    const int btnH = 28;
    const bool hovered = app.pointerX >= btnX && app.pointerY >= btnY &&
                          app.pointerX < btnX + btnW && app.pointerY < btnY + btnH;
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button btn;
    btn.setMinSize(0, 0);
    btn.setLabel("Reset to system");
    btn.setGeometry(static_cast<float>(btnX), static_cast<float>(btnY),
                    static_cast<float>(btnW), static_cast<float>(btnH));
    btn.setStyle(m3::Button::Style::Outlined);
    btn.setSize(m3::Button::Size::XS);
    btn.setAccentColor(a_r, a_g, a_b);
    btn.setOutlineColor(o_r, o_g, o_b);
    btn.setHovered(hovered);
    btn.paint(cr);
  }
}

// Pointer handler.
static bool time_toggle_hit(App& app, int cardX, int cardW, int rowIndex, bool& outNewVal) {
  constexpr int swW = 52;
  constexpr int swH = 26;
  const int swX = cardX + cardW - swW - kSpacingXL;
  const int swY = (kBodyTop + rowIndex * kSliderRowH) - 20;
  const double ly = app.pointerY + settings_scroll_px(app);
  if (!point_in_rect(app.pointerX, ly, swX, swY, swW, swH))
    return false;
  if (rowIndex == 0) outNewVal = !app.settings.timeUse24h;
  else if (rowIndex == 1) outNewVal = !app.settings.timeShowSeconds;
  else if (rowIndex == 2) outNewVal = !app.settings.timeShowDate;
  else return false;
  return true;
}

bool settings_time_consume_pointer_down(App& app, int contentX, int contentW) {
  const int cardXi = contentX + 8;
  const int cardWi = contentW - 16;
  const double ly = app.pointerY + settings_scroll_px(app);

  // Time format toggles
  {
    bool newVal = false;
    if (time_toggle_hit(app, cardXi, cardWi, 0, newVal)) {
      app.settings.timeUse24h = newVal;
      save_settings(app.settings);
      draw(app);
      return true;
    }
    if (time_toggle_hit(app, cardXi, cardWi, 1, newVal)) {
      app.settings.timeShowSeconds = newVal;
      save_settings(app.settings);
      draw(app);
      return true;
    }
    if (time_toggle_hit(app, cardXi, cardWi, 2, newVal)) {
      app.settings.timeShowDate = newVal;
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  // Date format combo
  {
    int cbx, cby, cbw, cbh;
    time_date_format_combo_geom(contentX, contentW, cbx, cby, cbw, cbh);
    if (point_in_rect(app.pointerX, ly, cbx, cby, cbw, cbh)) {
      app.timeFormatDropdownOpen = !app.timeFormatDropdownOpen;
      if (!app.timeFormatDropdownOpen) app.timeFormatDropdownHoverRow = -1;
      draw(app);
      return true;
    }
  }

  // Date format dropdown popup hits
  if (app.timeFormatDropdownOpen) {
    int cbx, cby, cbw, cbh;
    time_date_format_combo_geom(contentX, contentW, cbx, cby, cbw, cbh);
    const int listTop = cby + cbh + 2;
    const int rowH = kSettingsDdRowH;
    const int listH = kDateFormatCount * rowH;
    if (point_in_rect(app.pointerX, ly, cbx, listTop, cbw, listH)) {
      const int relY = static_cast<int>(ly) - listTop;
      const int idx = relY / rowH;
      if (idx >= 0 && idx < kDateFormatCount) {
        app.settings.timeDateFormat = idx;
        if (idx != 3) app.settings.timeCustomFormat.clear();
        app.timeFormatDropdownOpen = false;
        app.timeFormatDropdownHoverRow = -1;
        save_settings(app.settings);
        draw(app);
        return true;
      }
    }
    // Click outside closes
    if (!point_in_rect(app.pointerX, ly, cbx, cby - 4, cbw, listTop - cby + listH + 8)) {
      app.timeFormatDropdownOpen = false;
      app.timeFormatDropdownHoverRow = -1;
      draw(app);
      return true;
    }
  }

  // Reset timezone button
  if (!app.settings.timeTimezone.empty()) {
    const int btnY = (kTimezoneBodyTop + kSliderRowH + 4);
    const int btnX = contentX + kCardPad;
    const int btnW = 160;
    const int btnH = 28;
    if (point_in_rect(app.pointerX, ly, btnX, btnY, btnW, btnH)) {
      app.settings.timeTimezone.clear();
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  return false;
}
