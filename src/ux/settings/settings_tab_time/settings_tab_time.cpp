#include <cairo/cairo.h>
#include <wayland-client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include <xkbcommon/xkbcommon-keysyms.h>

#include "m3/core/primitives/box.hpp"
#include "m3/controls/containers/button.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_time/settings_tab_time.hpp"
#include "ux/settings/settings_tab_accounts/accounts_users.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

static constexpr int kCardTop = kContentTop;
static constexpr int kBodyTop = kCardTop + 52;
static constexpr int kFormatCardH = 52 + 3 * kSliderRowH + kSpacingXL;

static constexpr int kDateCardTop = kCardTop + kFormatCardH + kCardGap;
static constexpr int kDateBodyTop = kDateCardTop + 52;

static constexpr int kPreviewCardH = 52 + 80 + kSpacingXL;

// Status strip convention (matches the accounts tab): a 12px status line
// sits with its baseline 21px below the last control; the card extends 35px
// below it, so feedback text never paints over buttons.
static constexpr int kStatusDy = 21;
static constexpr int kStatusPad = 35;

// Dynamic layout: the date card grows when the custom-format editor is shown,
// the NTP card grows with the manual-clock editor (NTP off), and the
// timezone card grows with the set-as-system row + status strip.
int time_date_card_h(const App& app) {
  return 52 + kSliderRowH + kSpacingXL + (app.settings.timeDateFormat == 3 ? 56 : 0);
}

int time_preview_top(const App& app) {
  return kDateCardTop + time_date_card_h(app) + kCardGap;
}

int time_ntp_top(const App& app) {
  return time_preview_top(app) + kPreviewCardH + kCardGap;
}

static int time_ntp_card_h(const App& app) {
  if (app.timeNtpOn) return 52 + kSliderRowH + 28;
  // Manual editor: row + status strip below the regular status line.
  return 52 + kSliderRowH + 28 + 8 + 34 + 8 + 20 + 12;
}

int time_timezone_top(const App& app) {
  return time_ntp_top(app) + time_ntp_card_h(app) + kCardGap;
}

int time_timezone_body_top(const App& app) {
  return time_timezone_top(app) + 52;
}

static int time_timezone_card_h(const App& app) {
  const bool ov = !app.settings.timeTimezone.empty();
  // Buttons end at body+84+28 (+38 more with the set-as-system row).
  const int lastBottom = 52 + 84 + 28 + (ov ? 38 : 0);
  return lastBottom + kStatusPad;
}

int time_content_bottom(const App& app) {
  return time_timezone_top(app) + time_timezone_card_h(app);
}

// Document-space geometry for the NTP/timezone cards, shared by paint and
// hit-testing so the two can never drift apart.
struct TimeLayout {
  int cardX = 0, cardW = 0, innerW = 0;
  int ntpTop = 0, ntpBody = 0, ntpH = 0, ntpStatusY = 0;
  int refreshX = 0, refreshY = 0, refreshW = 110, refreshH = 28;
  bool ntpOn = false;
  int manualY = 0, manualDateW = 150, manualTimeW = 120;
  int manualTimeX = 0, manualSetX = 0, manualSetW = 130, manualStatusY = 0;
  int tzTop = 0, tzBody = 0, tzH = 0;
  int tzSysY = 0, tzOvY = 0, tzBtnY1 = 0, tzBtnY2 = 0, tzStatusY = 0;
  int tzChangeW = 130, tzResetW = 160, tzSetSysW = 240;
  bool hasOverride = false;
};

static TimeLayout time_layout(App& app, int contentX, int contentW) {
  TimeLayout L;
  L.cardX = contentX + 8;
  L.cardW = contentW - 16;
  L.innerW = L.cardW - 2 * kCardPad;
  L.ntpOn = app.timeNtpOn;
  L.ntpTop = time_ntp_top(app);
  L.ntpBody = L.ntpTop + 52;
  L.ntpStatusY = L.ntpBody + kSliderRowH + 2;
  // Refresh sits left of the toggle, vertically centered with it.
  {
    const int swX = L.cardX + L.cardW - 52 - kSpacingXL;
    const int swY = (L.ntpBody - 34) + (54 - 26) / 2;
    L.refreshW = 110;
    L.refreshH = 28;
    L.refreshX = swX - 12 - L.refreshW;
    L.refreshY = swY + (26 - L.refreshH) / 2;
  }
  if (!L.ntpOn) {
    L.manualY = L.ntpBody + kSliderRowH + 28 + 8;
    L.manualTimeX = contentX + kCardPad + L.manualDateW + 12;
    L.manualSetX = L.manualTimeX + L.manualTimeW + 12;
    L.manualStatusY = L.manualY + 34 + 8;
  }
  L.ntpH = time_ntp_card_h(app);
  L.hasOverride = !app.settings.timeTimezone.empty();
  L.tzTop = time_timezone_top(app);
  L.tzBody = L.tzTop + 52;
  L.tzSysY = L.tzBody + 30;
  L.tzOvY = L.tzBody + 52;
  L.tzBtnY1 = L.tzBody + 84;
  L.tzBtnY2 = L.tzBtnY1 + 28 + 10;
  const int lastBottom = L.hasOverride ? L.tzBtnY2 + 28 : L.tzBtnY1 + 28;
  L.tzStatusY = lastBottom + kStatusDy;
  L.tzH = lastBottom + kStatusPad - 52;
  return L;
}

// Cached system timezone (tick-refreshed; never queried per paint).
static std::string time_query_system_tz() {
  FILE* p = popen("timedatectl show -p Timezone --value 2>/dev/null", "r");
  std::string out;
  if (p) {
    char buf[128] = {};
    if (fgets(buf, sizeof(buf), p)) {
      out = buf;
      while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    }
    pclose(p);
  }
  return out;
}

static std::string time_cached_system_tz(bool force) {
  static std::string cache;
  static int tick = 0;
  static bool have = false;
  if (force || !have || (++tick % 300 == 0)) {
    cache = time_query_system_tz();
    have = true;
  }
  return cache;
}

static void time_ensure_zone_list(App& app);

static bool valid_zone_id(App& app, const std::string& zone) {
  if (zone.empty() || zone.size() > 64) return false;
  for (char c : zone) {
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
          c == '-' || c == '+' || c == '/'))
      return false;
  }
  if (zone.find("..") != std::string::npos) return false;
  time_ensure_zone_list(app);
  return std::find(app.timeZoneList.begin(), app.timeZoneList.end(), zone) != app.timeZoneList.end();
}

static bool valid_manual_date(const std::string& d, int& y, int& m, int& dd) {
  y = m = dd = 0;
  if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
  for (size_t i = 0; i < d.size(); ++i) {
    if (i == 4 || i == 7) continue;
    if (d[i] < '0' || d[i] > '9') return false;
  }
  y = (d[0] - '0') * 1000 + (d[1] - '0') * 100 + (d[2] - '0') * 10 + (d[3] - '0');
  m = (d[5] - '0') * 10 + (d[6] - '0');
  dd = (d[8] - '0') * 10 + (d[9] - '0');
  if (y < 1970 || y > 2100 || m < 1 || m > 12 || dd < 1) return false;
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int dim = kDays[m - 1];
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) dim = 29;
  return dd <= dim;
}

static bool valid_manual_time(const std::string& t, int& hh, int& mm, int& ss) {
  hh = mm = ss = 0;
  const bool hasSec = (t.size() == 8);
  if (t.size() != 5 && !hasSec) return false;
  if (t[2] != ':' || (hasSec && t[5] != ':')) return false;
  for (size_t i = 0; i < t.size(); ++i) {
    if (i == 2 || i == 5) continue;
    if (t[i] < '0' || t[i] > '9') return false;
  }
  hh = (t[0] - '0') * 10 + (t[1] - '0');
  mm = (t[3] - '0') * 10 + (t[4] - '0');
  if (hasSec) ss = (t[6] - '0') * 10 + (t[7] - '0');
  return hh <= 23 && mm <= 59 && ss <= 59;
}

static void time_action_set_manual(App& app) {
  int y = 0, mo = 0, d = 0, hh = 0, mm = 0, ss = 0;
  if (!valid_manual_date(app.timeManualDate, y, mo, d)) {
    app.timeManualMsg = "Use a real date as YYYY-MM-DD.";
    app.timeManualErr = true;
    draw(app);
    return;
  }
  if (!valid_manual_time(app.timeManualTime, hh, mm, ss)) {
    app.timeManualMsg = "Use HH:MM or HH:MM:SS.";
    app.timeManualErr = true;
    draw(app);
    return;
  }
  char arg[32];
  std::snprintf(arg, sizeof(arg), "%04d-%02d-%02d %02d:%02d:%02d", y, mo, d, hh, mm, ss);
  PrivResult r = accounts_run_priv({"pkexec", "timedatectl", "set-time", arg});
  if (r.status == 0) {
    app.timeManualMsg = "System clock set.";
    app.timeManualErr = false;
    app.timeManualActive = 0;
  } else {
    std::string first = r.output;
    const size_t nl = first.find('\n');
    if (nl != std::string::npos) first.resize(nl);
    app.timeManualMsg =
        first.empty() ? "Could not set the clock (administrator authentication may be required)."
                      : first;
    app.timeManualErr = true;
  }
  draw(app);
}

static void time_action_set_system_zone(App& app) {
  const std::string zone = app.settings.timeTimezone;
  if (!valid_zone_id(app, zone)) {
    app.timeZoneMsg = "Pick a timezone first.";
    app.timeZoneErr = true;
    draw(app);
    return;
  }
  PrivResult r = accounts_run_priv({"pkexec", "timedatectl", "set-timezone", zone});
  if (r.status == 0) {
    app.timeZoneMsg = "System timezone set to '" + zone + "'.";
    app.timeZoneErr = false;
    (void)time_cached_system_tz(true);
  } else {
    std::string first = r.output;
    const size_t nl = first.find('\n');
    if (nl != std::string::npos) first.resize(nl);
    app.timeZoneMsg =
        first.empty() ? "Could not set the timezone (administrator authentication may be required)."
                      : first;
    app.timeZoneErr = true;
  }
  draw(app);
}

void time_custom_format_field_geom(int contentX, int contentW, int& bx, int& by, int& bw, int& bh) {
  bx = contentX + kCardPad;
  by = kDateBodyTop + 26 + 28 + 12;
  bw = std::min(contentW - kCardPad * 2, 420);
  bh = 34;
}

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

  const int previewY = previewCardTop + 52;
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

// --- Timezone picker ------------------------------------------------------

static constexpr int kTzPickW = 480;
static constexpr int kTzPickH = 400;
static constexpr int kTzRowH = 38;

static void time_ensure_zone_list(App& app) {
  if (app.timeZoneListReady) return;
  app.timeZoneListReady = true;
  app.timeZoneList.clear();
  std::error_code ec;
  std::filesystem::recursive_directory_iterator it("/usr/share/zoneinfo",
                                                   std::filesystem::directory_options::skip_permission_denied, ec);
  if (ec) return;
  const std::filesystem::recursive_directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    std::error_code fec;
    if (!it->is_regular_file(fec) || fec) continue;
    std::string rel = it->path().lexically_relative("/usr/share/zoneinfo").string();
    if (rel.rfind("posix/", 0) == 0 || rel.rfind("right/", 0) == 0 || rel.rfind("SystemV/", 0) == 0) continue;
    if (rel.find('/') == std::string::npos && rel != "UTC") continue;
    if (rel == "localtime" || rel == "posixrules" || rel == "zone.tab" || rel == "zone1970.tab" ||
        rel == "tzdata.zi" || rel == "leapseconds" || rel == "leap-seconds.list" || rel == "iso3166.tab" ||
        rel == "+VERSION")
      continue;
    app.timeZoneList.push_back(rel);
  }
  std::sort(app.timeZoneList.begin(), app.timeZoneList.end());
}

static std::string time_zone_pretty(const std::string& id) {
  const size_t sl = id.rfind('/');
  std::string city = sl == std::string::npos ? id : id.substr(sl + 1);
  std::string area = sl == std::string::npos ? std::string() : id.substr(0, sl);
  for (auto& ch : city) {
    if (ch == '_') ch = ' ';
  }
  if (area.empty()) return city;
  return city + " (" + area + ")";
}

// Indices into timeZoneList matching the current filter (all when empty).
static void time_filtered_zones(App& app, std::vector<size_t>& out) {
  time_ensure_zone_list(app);
  out.clear();
  std::string needle = app.timeZoneFilter;
  std::transform(needle.begin(), needle.end(), needle.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (size_t i = 0; i < app.timeZoneList.size(); ++i) {
    if (needle.empty()) {
      out.push_back(i);
      continue;
    }
    const std::string& id = app.timeZoneList[i];
    std::string low = id;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string pretty = time_zone_pretty(id);
    std::transform(pretty.begin(), pretty.end(), pretty.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (low.find(needle) != std::string::npos || pretty.find(needle) != std::string::npos) out.push_back(i);
  }
}

static void time_pick_zone(App& app, size_t filtered_idx) {
  std::vector<size_t> vis;
  time_filtered_zones(app, vis);
  if (filtered_idx >= vis.size()) return;
  app.settings.timeTimezone = app.timeZoneList[vis[filtered_idx]];
  save_settings(app.settings);
  app.timeZonePickerOpen = false;
  app.timeZoneFilter.clear();
  app.timeZoneHoverRow = -1;
  app.timeZoneScrollPx = 0;
  draw(app);
}

static void time_zone_picker_geom(const App& app, int& px, int& py, int& pw, int& ph) {
  pw = kTzPickW;
  ph = kTzPickH;
  px = (app.width - pw) / 2;
  py = std::max(30, (app.height - ph) / 2);
}

bool time_zone_picker_visible(const App& app) {
  return app.activeTab == 30 && app.timeZonePickerOpen;
}

bool time_zone_picker_consume_scroll(App& app, double delta_px) {
  if (!app.timeZonePickerOpen) return false;
  int px = 0, py = 0, pw = 0, ph = 0;
  time_zone_picker_geom(app, px, py, pw, ph);
  if (app.pointerX < px || app.pointerY < py || app.pointerX >= px + pw || app.pointerY >= py + ph)
    return false;
  std::vector<size_t> vis;
  time_filtered_zones(app, vis);
  const int listH = ph - 90 - 12;
  const int maxScroll = std::max(0, static_cast<int>(vis.size()) * kTzRowH - listH);
  const int step = static_cast<int>(std::lround(delta_px));
  if (step == 0) return true;
  app.timeZoneScrollPx = std::clamp(app.timeZoneScrollPx - step, 0, maxScroll);
  draw(app);
  return true;
}

void paint_time_zone_picker(App& app, cairo_t* cr) {
  int px = 0, py = 0, pw = 0, ph = 0;
  time_zone_picker_geom(app, px, py, pw, ph);

  cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);

  float ar, ag, ab, tr, tg, tb, sr, sg, sb, or_, og, ob;
  settings_resolve_colors(app, ar, ag, ab, tr, tg, tb, sr, sg, sb, or_, og, ob);
  settings_card(app, cr, px, py, pw, ph, 1.0);
  settings_show_text(cr, px + kCardPad, py + 30, "Select Timezone", 16.f, 600, tr, tg, tb, 0.92f);

  // Search field.
  const int sbX = px + kCardPad;
  const int sbY = py + 44;
  const int sbW = pw - kCardPad * 2;
  const int sbH = 34;
  {
    m3::Box box;
    box.setColor(sr, sg, sb, 0.55f);
    box.setRadius(8.f);
    box.setGeometry(sbX, sbY, sbW, sbH);
    box.paint(cr);
    paint_src_accent(app, cr, 0.7);
    cairo_set_line_width(cr, 1.5);
    cairo_round_rect(cr, sbX, sbY, sbW, sbH, 8.0);
    cairo_stroke(cr);
  }
  material_symbols_draw_glyph(cr, sbX + 18.0, sbY + sbH * 0.5, 16.0, "search", tr, tg, tb, 0.55f);
  if (app.timeZoneFilter.empty()) {
    settings_show_text(cr, sbX + 38.0, sbY + sbH * 0.5 + 5.0, "Type a city or region\u2026", 13.f, 400, tr, tg, tb,
                       0.35f);
  } else {
    settings_show_text(cr, sbX + 38.0, sbY + sbH * 0.5 + 5.0, app.timeZoneFilter.c_str(), 13.f, 400, tr, tg, tb,
                       0.90f);
  }

  std::vector<size_t> vis;
  time_filtered_zones(app, vis);
  const int listY = py + 90;
  const int listH = ph - 90 - 12;
  const int maxScroll = std::max(0, static_cast<int>(vis.size()) * kTzRowH - listH);
  app.timeZoneScrollPx = std::clamp(app.timeZoneScrollPx, 0, maxScroll);

  cairo_save(cr);
  cairo_rectangle(cr, px + 8, listY, pw - 16, listH);
  cairo_clip(cr);
  const int startIdx = app.timeZoneScrollPx / kTzRowH;
  for (size_t vi = static_cast<size_t>(startIdx); vi < vis.size(); ++vi) {
    const int ry = listY + static_cast<int>(vi) * kTzRowH - app.timeZoneScrollPx;
    if (ry >= listY + listH) break;
    const std::string& id = app.timeZoneList[vis[vi]];
    const bool hov = app.timeZoneHoverRow == static_cast<int>(vi);
    const bool cur = id == app.settings.timeTimezone;
    if (hov) {
      m3::Box hb;
      hb.setColor(tr, tg, tb, 0.07f);
      hb.setRadius(6.0f);
      hb.setGeometry(static_cast<float>(px + kCardPad), static_cast<float>(ry + 1),
                     static_cast<float>(pw - kCardPad * 2), static_cast<float>(kTzRowH - 2));
      hb.paint(cr);
    }
    std::string pretty = time_zone_pretty(id);
    m3::Label lbl;
    lbl.setText(pretty.c_str());
    lbl.setFontSize(13.0f);
    lbl.setFontWeight(cur ? 700 : 400);
    lbl.setColor(tr, tg, tb, hov || cur ? 0.95f : 0.80f);
    float tw, th;
    lbl.measureExtents(tw, th);
    lbl.paintAt(cr, static_cast<float>(px + kCardPad + 8),
                static_cast<float>(ry) + (static_cast<float>(kTzRowH) - th) * 0.5f);
    if (cur) {
      material_symbols_draw_glyph(cr, static_cast<double>(px + pw - kCardPad - 12),
                                  static_cast<double>(ry) + kTzRowH * 0.5, 16.0, "check", ar, ag, ab, 0.9f);
    }
  }
  if (vis.empty()) {
    settings_show_text(cr, px + kCardPad + 8, listY + 24, "No timezones match.", 13.f, 400, tr, tg, tb, 0.45f);
  }
  cairo_restore(cr);

  // Scrollbar.
  const int totalH = static_cast<int>(vis.size()) * kTzRowH;
  if (totalH > listH) {
    const double frac = static_cast<double>(app.timeZoneScrollPx) / static_cast<double>(totalH - listH);
    const double sbHgt = std::max(24.0, static_cast<double>(listH) * listH / totalH);
    const double sbY = listY + frac * (listH - sbHgt);
    cairo_set_source_rgba(cr, tr, tg, tb, 0.20);
    cairo_round_rect(cr, px + pw - 12, sbY, 4.0, sbHgt, 2.0);
    cairo_fill(cr);
  }
}

bool time_zone_picker_consume_pointer_down(App& app) {
  int px = 0, py = 0, pw = 0, ph = 0;
  time_zone_picker_geom(app, px, py, pw, ph);
  if (!point_in_rect(app.pointerX, app.pointerY, px, py, pw, ph)) {
    app.timeZonePickerOpen = false;
    app.timeZoneFilter.clear();
    app.timeZoneHoverRow = -1;
    draw(app);
    return true;
  }
  std::vector<size_t> vis;
  time_filtered_zones(app, vis);
  const int listY = py + 90;
  const int listH = ph - 90 - 12;
  const int relY = static_cast<int>(app.pointerY) - listY + app.timeZoneScrollPx;
  const int idx = relY / kTzRowH;
  if (idx >= 0 && static_cast<size_t>(idx) < vis.size() && app.pointerY < listY + listH) {
    time_pick_zone(app, static_cast<size_t>(idx));
    return true;
  }
  return true;
}

bool time_zone_picker_consume_pointer_move(App& app) {
  int px = 0, py = 0, pw = 0, ph = 0;
  time_zone_picker_geom(app, px, py, pw, ph);
  std::vector<size_t> vis;
  time_filtered_zones(app, vis);
  const int listY = py + 90;
  const int listH = ph - 90 - 12;
  int nh = -1;
  if (point_in_rect(app.pointerX, app.pointerY, px + kCardPad, listY, pw - kCardPad * 2, listH)) {
    const int relY = static_cast<int>(app.pointerY) - listY + app.timeZoneScrollPx;
    const int idx = relY / kTzRowH;
    if (idx >= 0 && static_cast<size_t>(idx) < vis.size()) nh = idx;
  }
  if (nh != app.timeZoneHoverRow) {
    app.timeZoneHoverRow = nh;
    draw(app);
  }
  return true;
}

// --- NTP (timedatectl) ------------------------------------------------------

static std::string time_run_capture(const std::string& cmd) {
  std::string out;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) return out;
  char buf[256];
  while (fgets(buf, sizeof(buf), p)) {
    out += buf;
    if (out.size() > 512) break;
  }
  pclose(p);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
  return out;
}

static void time_query_ntp(App& app) {
  app.timeNtpQueried = true;
  app.timeNtpOn = false;
  app.timeNtpSynced = false;
  const std::string ntp =
      time_run_capture("command -v timedatectl >/dev/null 2>&1 && timedatectl show -p NTP --value 2>&1");
  if (ntp == "yes") {
    app.timeNtpOn = true;
    const std::string sync =
        time_run_capture("timedatectl show -p NTPSynchronized --value 2>/dev/null");
    app.timeNtpSynced = (sync == "yes");
    app.timeNtpMsg.clear();
  } else if (ntp == "no") {
    app.timeNtpOn = false;
    app.timeNtpMsg.clear();
  } else {
    app.timeNtpMsg = "timedatectl is not available on this system.";
  }
}

static void time_set_ntp(App& app, bool on) {
  const std::string out =
      time_run_capture(std::string("timedatectl set-ntp ") + (on ? "true" : "false") + " 2>&1");
  time_query_ntp(app);
  if (app.timeNtpOn != on) {
    std::string first = out;
    const size_t nl = first.find('\n');
    if (nl != std::string::npos) first.resize(nl);
    app.timeNtpMsg = first.empty() ? "Could not change NTP (administrator authentication may be required)."
                                   : first;
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
                static_cast<double>(cardW), static_cast<double>(time_date_card_h(app)), glassOv);
  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), kDateCardTop + 22, "DATE FORMAT");

  settings_label(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(kDateBodyTop),
                 "Date display style",
                 "How dates appear alongside the clock");

  int cbx, cby, cbw, cbh;
  time_date_format_combo_geom(contentX, contentW, cbx, cby, cbw, cbh);
  settings_paint_combo_closed(app, cr, cbx, cby, cbw, cbh, glassOv,
                               date_format_label(app.settings.timeDateFormat), false);

  // Custom strftime editor (only for the Custom style).
  if (app.settings.timeDateFormat == 3) {
    int fbx, fby, fbw, fbh;
    time_custom_format_field_geom(contentX, contentW, fbx, fby, fbw, fbh);
    const bool fActive = app.timeCustomFormatActive;
    m3::Box fbg;
    float fr, fg, fb;
    if (app.drawChromeMatugen) {
      fr = app.drawChrome.panelFillR;
      fg = app.drawChrome.panelFillG;
      fb = app.drawChrome.panelFillB;
    } else {
      fr = static_cast<float>(Theme::BgR);
      fg = static_cast<float>(Theme::BgG);
      fb = static_cast<float>(Theme::BgB);
    }
    fbg.setColor(fr, fg, fb, 0.55f);
    fbg.setRadius(8.f);
    fbg.setGeometry(fbx, fby, fbw, fbh);
    fbg.paint(cr);
    if (fActive) {
      paint_src_accent(app, cr, 0.7);
      cairo_set_line_width(cr, 1.5);
    } else {
      cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.12);
      cairo_set_line_width(cr, 1.0);
    }
    cairo_round_rect(cr, fbx, fby, fbw, fbh, 8.0);
    cairo_stroke(cr);
    if (app.settings.timeCustomFormat.empty() && !fActive) {
      settings_show_text(cr, fbx + 10.0, fby + fbh * 0.5 + 5.0, "%a %d", 13.f, 400, Theme::TextR,
                         Theme::TextG, Theme::TextB, 0.30f);
    } else {
      settings_show_text(cr, fbx + 10.0, fby + fbh * 0.5 + 5.0, app.settings.timeCustomFormat.c_str(), 13.f,
                         400, Theme::TextR, Theme::TextG, Theme::TextB, 0.90f);
    }
  }

  // Preview card.
  const int previewTop = time_preview_top(app);
  settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(previewTop),
                static_cast<double>(cardW), static_cast<double>(kPreviewCardH), glassOv);
  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), previewTop + 22, "PREVIEW");

  paint_time_preview(app, cr, contentX, cardX, cardW, previewTop);

  // Network time card.
  static int s_ntpTick = 0;
  if (!app.timeNtpQueried || (++s_ntpTick % 300 == 0)) time_query_ntp(app);
  const TimeLayout L = time_layout(app, contentX, contentW);
  const int ntpTop = L.ntpTop;
  const int ntpBody = L.ntpBody;
  settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(ntpTop),
                static_cast<double>(cardW), static_cast<double>(L.ntpH), glassOv);
  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), ntpTop + 22, "NETWORK TIME");
  settings_label(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(ntpBody),
                 "Automatic time (NTP)",
                 "Set the clock from the network (needs administrator authentication to change)");
  settings_toggle(app, cr, cardX, ntpBody, cardW,
                   static_cast<double>(ntpBody - 34), 54.0, app.timeNtpOn, 0.0);
  // Refresh button, top-right of the card.
  {
    m3::Button btn;
    btn.setMinSize(0, 0);
    btn.setLabel("Refresh");
    btn.setGeometry(static_cast<float>(L.refreshX), static_cast<float>(L.refreshY),
                    static_cast<float>(L.refreshW), static_cast<float>(L.refreshH));
    btn.setStyle(m3::Button::Style::Outlined);
    btn.setSize(m3::Button::Size::XS);
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    btn.setAccentColor(a_r, a_g, a_b);
    btn.setTextColor(t_r, t_g, t_b);
    btn.setOutlineColor(o_r, o_g, o_b);
    const double ly0 = app.pointerY + settings_scroll_px(app);
    btn.setHovered(app.pointerX >= L.refreshX && ly0 >= L.refreshY &&
                   app.pointerX < L.refreshX + L.refreshW && ly0 < L.refreshY + L.refreshH);
    btn.paint(cr);
  }
  {
    std::string ntpStatus;
    if (!app.timeNtpMsg.empty())
      ntpStatus = app.timeNtpMsg;
    else if (app.timeNtpOn)
      ntpStatus = app.timeNtpSynced ? "Synchronized with network time." : "Waiting for synchronization.";
    else
      ntpStatus = "Manual clock. Automatic time is off.";
    settings_show_text(cr, contentX + kCardPad, L.ntpStatusY, ntpStatus.c_str(), 11, 400,
                       Theme::TextR, Theme::TextG, Theme::TextB, 0.46);
  }

  // Manual clock editor (NTP off only).
  if (!L.ntpOn) {
    auto field = [&](int fx, int fw, const std::string& text, bool active, const char* placeholder) {
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
      box.setColor(r, g, b, 0.55f);
      box.setRadius(8.f);
      box.setGeometry(fx, L.manualY, fw, 34);
      box.paint(cr);
      if (active) {
        paint_src_accent(app, cr, 0.7);
        cairo_set_line_width(cr, 1.5);
      } else {
        cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.12);
        cairo_set_line_width(cr, 1.0);
      }
      cairo_round_rect(cr, fx, L.manualY, fw, 34, 8.0);
      cairo_stroke(cr);
      if (text.empty() && !active) {
        settings_show_text(cr, fx + 10.0, L.manualY + 22.0, placeholder, 13.f, 400,
                           Theme::TextR, Theme::TextG, Theme::TextB, 0.30f);
      } else {
        settings_show_text(cr, fx + 10.0, L.manualY + 22.0, text.c_str(), 13.f, 400,
                           Theme::TextR, Theme::TextG, Theme::TextB, 0.88f);
        if (active) {
          auto* layout = pango_cairo_create_layout(cr);
          auto* desc = pango_font_description_new();
          pango_font_description_set_family(desc, "Inter");
          pango_font_description_set_size(desc, static_cast<int>(13.0 * PANGO_SCALE));
          pango_layout_set_font_description(layout, desc);
          pango_layout_set_text(layout, text.c_str(), -1);
          int pw = 0, ph = 0;
          pango_layout_get_pixel_size(layout, &pw, &ph);
          pango_font_description_free(desc);
          g_object_unref(layout);
          (void)ph;
          cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
          cairo_set_font_size(cr, 13.0);
          cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.60);
          cairo_move_to(cr, std::min(fx + 10.0 + pw + 2.0, fx + fw - 6.0), L.manualY + 22.0);
          cairo_show_text(cr, "|");
        }
      }
    };
    const int colX = contentX + kCardPad;
    field(colX, L.manualDateW, app.timeManualDate, app.timeManualActive == 1, "YYYY-MM-DD");
    field(L.manualTimeX, L.manualTimeW, app.timeManualTime, app.timeManualActive == 2, "HH:MM:SS");
    {
      m3::Button btn;
      btn.setMinSize(0, 0);
      btn.setLabel("Set Clock");
      btn.setGeometry(static_cast<float>(L.manualSetX), static_cast<float>(L.manualY),
                      static_cast<float>(L.manualSetW), 34.0f);
      btn.setStyle(m3::Button::Style::Outlined);
      btn.setSize(m3::Button::Size::XS);
      float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
      settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
      btn.setAccentColor(a_r, a_g, a_b);
      btn.setTextColor(t_r, t_g, t_b);
      btn.setOutlineColor(o_r, o_g, o_b);
      const double ly0 = app.pointerY + settings_scroll_px(app);
      btn.setHovered(app.pointerX >= L.manualSetX && ly0 >= L.manualY &&
                     app.pointerX < L.manualSetX + L.manualSetW && ly0 < L.manualY + 34);
      btn.paint(cr);
    }
    if (!app.timeManualMsg.empty()) {
      if (app.timeManualErr)
        settings_show_text(cr, colX, L.manualStatusY, app.timeManualMsg.c_str(), 12.f, 400,
                           1.0f, 0.45f, 0.40f, 0.9f);
      else
        settings_show_text(cr, colX, L.manualStatusY, app.timeManualMsg.c_str(), 12.f, 400,
                           Theme::AccR, Theme::AccG, Theme::AccB, 0.85f);
    }
  }

  // Timezone card.
  const int tzTop = L.tzTop;
  const int tzBody = L.tzBody;
  settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(tzTop),
                static_cast<double>(cardW), static_cast<double>(L.tzH), glassOv);
  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), tzTop + 22, "TIMEZONE");

  settings_label(cr, static_cast<double>(contentX + kCardPad), static_cast<double>(tzBody),
                 "Timezone",
                 "Applies to this desktop's clocks. Set as system to change the whole machine.");

  // System timezone line (cached; never queried per paint).
  {
    const std::string sysTz = time_cached_system_tz(false);
    const std::string line = sysTz.empty() ? "System: unknown" : ("System: " + sysTz);
    settings_show_text(cr, contentX + kCardPad, L.tzSysY, line.c_str(), 12.f, 400,
                       Theme::TextR, Theme::TextG, Theme::TextB, 0.50f);
  }

  // Override line: the picked zone, or "using system".
  {
    std::string tzLine;
    if (!app.settings.timeTimezone.empty()) {
      tzLine = app.settings.timeTimezone;
      if (std::chrono::locate_zone(app.settings.timeTimezone) == nullptr) tzLine += " (unknown timezone)";
    } else {
      tzLine = "Using system timezone";
    }
    settings_show_text(cr, contentX + kCardPad, L.tzOvY, tzLine.c_str(), 14, 700, Theme::AccR, Theme::AccG,
                       Theme::AccB, 0.90);
  }

  auto tz_button = [&](int bx, int by, const char* label, int bw) {
    const double hy = app.pointerY + settings_scroll_px(app);
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button btn;
    btn.setMinSize(0, 0);
    btn.setLabel(label);
    btn.setGeometry(static_cast<float>(bx), static_cast<float>(by),
                    static_cast<float>(bw), 28.0f);
    btn.setStyle(m3::Button::Style::Outlined);
    btn.setSize(m3::Button::Size::XS);
    btn.setAccentColor(a_r, a_g, a_b);
    btn.setOutlineColor(o_r, o_g, o_b);
    btn.setHovered(app.pointerX >= bx && hy >= by && app.pointerX < bx + bw && hy < by + 28);
    btn.paint(cr);
  };

  // Change / Reset buttons (row 1).
  tz_button(contentX + kCardPad, L.tzBtnY1, "Change\u2026", L.tzChangeW);
  if (L.hasOverride) {
    tz_button(contentX + kCardPad + L.tzChangeW + 12, L.tzBtnY1, "Reset", L.tzResetW);
    // Set-as-system button (row 2).
    tz_button(contentX + kCardPad, L.tzBtnY2, "Set as System Timezone", L.tzSetSysW);
  }
  if (!app.timeZoneMsg.empty()) {
    if (app.timeZoneErr)
      settings_show_text(cr, contentX + kCardPad, L.tzStatusY, app.timeZoneMsg.c_str(), 12.f, 400,
                         1.0f, 0.45f, 0.40f, 0.9f);
    else
      settings_show_text(cr, contentX + kCardPad, L.tzStatusY, app.timeZoneMsg.c_str(), 12.f, 400,
                         Theme::AccR, Theme::AccG, Theme::AccB, 0.85f);
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

  // Timezone picker overlay consumes everything while open.
  if (app.activeTab == 30 && app.timeZonePickerOpen) {
    time_zone_picker_consume_pointer_down(app);
    return true;
  }

  // Custom-format field focus (clicking elsewhere commits + defocuses).
  if (app.settings.timeDateFormat == 3) {
    int fbx, fby, fbw, fbh;
    time_custom_format_field_geom(contentX, contentW, fbx, fby, fbw, fbh);
    const double fly = static_cast<double>(fby) - settings_scroll_px(app);
    if (app.pointerX >= fbx && app.pointerY >= fly && app.pointerX < fbx + fbw && app.pointerY < fly + fbh) {
      app.timeCustomFormatActive = true;
      app.timeManualActive = 0;
      draw(app);
      return true;
    }
    if (app.timeCustomFormatActive) {
      app.timeCustomFormatActive = false;
      save_settings(app.settings);
      draw(app);
    }
  }

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

  // NTP toggle (geometry mirrors settings_toggle placement)
  {
    constexpr int swW = 52;
    constexpr int swH = 26;
    const TimeLayout Ln = time_layout(app, contentX, contentW);
    const int ntpBody = Ln.ntpBody;
    const int swX = cardXi + cardWi - swW - kSpacingXL;
    const int swY = (ntpBody - 34) + (54 - swH) / 2;
    if (point_in_rect(app.pointerX, ly, swX, swY, swW, swH)) {
      time_set_ntp(app, !app.timeNtpOn);
      app.timeManualActive = 0;
      draw(app);
      return true;
    }
    // Refresh button.
    if (point_in_rect(app.pointerX, ly, Ln.refreshX, Ln.refreshY, Ln.refreshW, Ln.refreshH)) {
      time_query_ntp(app);
      draw(app);
      return true;
    }
    // Manual clock editor (NTP off only).
    if (!Ln.ntpOn) {
      if (point_in_rect(app.pointerX, ly, contentX + kCardPad, Ln.manualY, Ln.manualDateW, 34)) {
        app.timeManualActive = 1;
        app.timeCustomFormatActive = false;
        draw(app);
        return true;
      }
      if (point_in_rect(app.pointerX, ly, Ln.manualTimeX, Ln.manualY, Ln.manualTimeW, 34)) {
        app.timeManualActive = 2;
        app.timeCustomFormatActive = false;
        draw(app);
        return true;
      }
      if (point_in_rect(app.pointerX, ly, Ln.manualSetX, Ln.manualY, Ln.manualSetW, 34)) {
        app.timeManualActive = 0;
        time_action_set_manual(app);
        return true;
      }
      if (app.timeManualActive != 0) {
        app.timeManualActive = 0;
        draw(app);
      }
    }
  }

  // Change timezone button
  {
    const TimeLayout Lz = time_layout(app, contentX, contentW);
    const int btnX = contentX + kCardPad;
    if (point_in_rect(app.pointerX, ly, btnX, Lz.tzBtnY1, Lz.tzChangeW, 28)) {
      time_ensure_zone_list(app);
      app.timeZonePickerOpen = true;
      app.timeZoneFilter.clear();
      app.timeZoneHoverRow = -1;
      app.timeZoneScrollPx = 0;
      draw(app);
      return true;
    }
  }

  // Reset timezone button
  if (!app.settings.timeTimezone.empty()) {
    const TimeLayout Lz = time_layout(app, contentX, contentW);
    const int btnX = contentX + kCardPad + Lz.tzChangeW + 12;
    if (point_in_rect(app.pointerX, ly, btnX, Lz.tzBtnY1, Lz.tzResetW, 28)) {
      app.settings.timeTimezone.clear();
      app.timeZoneMsg.clear();
      save_settings(app.settings);
      draw(app);
      return true;
    }
    // Set-as-system-timezone button (row 2).
    if (point_in_rect(app.pointerX, ly, contentX + kCardPad, Lz.tzBtnY2, Lz.tzSetSysW, 28)) {
      time_action_set_system_zone(app);
      return true;
    }
  }

  return false;
}

bool settings_time_consume_key(App& app, unsigned sym, unsigned state, const char* utf8, int utf8Len) {
  if (app.activeTab != 30) return false;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED && state != WL_KEYBOARD_KEY_STATE_REPEATED) return false;

  // Timezone picker: type to filter, arrows to move, Enter to pick, Esc to close.
  if (app.timeZonePickerOpen) {
    if (sym == XKB_KEY_Escape) {
      app.timeZonePickerOpen = false;
      app.timeZoneFilter.clear();
      app.timeZoneHoverRow = -1;
      draw(app);
      return true;
    }
    std::vector<size_t> vis;
    time_filtered_zones(app, vis);
    const int n = static_cast<int>(vis.size());
    if (sym == XKB_KEY_Up || sym == XKB_KEY_Down) {
      int hv = app.timeZoneHoverRow;
      if (sym == XKB_KEY_Up)
        hv = (hv <= 0) ? n - 1 : hv - 1;
      else
        hv = (hv < 0 || hv >= n - 1) ? 0 : hv + 1;
      app.timeZoneHoverRow = (n == 0) ? -1 : hv;
      if (hv >= 0) {
        int px = 0, py = 0, pw = 0, ph = 0;
        time_zone_picker_geom(app, px, py, pw, ph);
        const int listH = ph - 90 - 12;
        const int rowTop = hv * kTzRowH;
        if (rowTop < app.timeZoneScrollPx)
          app.timeZoneScrollPx = rowTop;
        else if (rowTop + kTzRowH > app.timeZoneScrollPx + listH)
          app.timeZoneScrollPx = rowTop + kTzRowH - listH;
      }
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      if (app.timeZoneHoverRow >= 0) time_pick_zone(app, static_cast<size_t>(app.timeZoneHoverRow));
      return true;
    }
    if (sym == XKB_KEY_BackSpace) {
      while (!app.timeZoneFilter.empty() && (app.timeZoneFilter.back() & 0xC0) == 0x80)
        app.timeZoneFilter.pop_back();
      if (!app.timeZoneFilter.empty()) app.timeZoneFilter.pop_back();
      app.timeZoneScrollPx = 0;
      app.timeZoneHoverRow = -1;
      draw(app);
      return true;
    }
    if (utf8Len > 0 && utf8 && app.timeZoneFilter.size() < 48) {
      app.timeZoneFilter.append(utf8, static_cast<size_t>(utf8Len));
      app.timeZoneScrollPx = 0;
      app.timeZoneHoverRow = -1;
      draw(app);
    }
    return true;
  }

  // Manual clock fields (NTP off).
  if (app.timeManualActive == 1 || app.timeManualActive == 2) {
    if (sym == XKB_KEY_Escape) {
      app.timeManualActive = 0;
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      time_action_set_manual(app);
      return true;
    }
    if (sym == XKB_KEY_Tab && state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      app.timeManualActive = (app.timeManualActive == 1) ? 2 : 1;
      draw(app);
      return true;
    }
    std::string& buf = (app.timeManualActive == 1) ? app.timeManualDate : app.timeManualTime;
    const size_t cap = (app.timeManualActive == 1) ? 10 : 8;
    if (sym == XKB_KEY_BackSpace) {
      while (!buf.empty() && (buf.back() & 0xC0) == 0x80) buf.pop_back();
      if (!buf.empty()) buf.pop_back();
      draw(app);
      return true;
    }
    if (utf8Len > 0 && utf8) {
      for (int i = 0; i < utf8Len && buf.size() < cap; ++i) {
        const char c = utf8[i];
        const bool ok = (app.timeManualActive == 1)
                            ? ((c >= '0' && c <= '9') || c == '-')
                            : ((c >= '0' && c <= '9') || c == ':');
        if (ok) buf.push_back(c);
      }
      draw(app);
    }
    return true;
  }

  // Custom strftime field.
  if (app.timeCustomFormatActive) {
    if (sym == XKB_KEY_Escape || sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      app.timeCustomFormatActive = false;
      save_settings(app.settings);
      draw(app);
      return true;
    }
    if (sym == XKB_KEY_BackSpace) {
      auto& s = app.settings.timeCustomFormat;
      while (!s.empty() && (s.back() & 0xC0) == 0x80) s.pop_back();
      if (!s.empty()) s.pop_back();
      draw(app);
      return true;
    }
    if (utf8Len > 0 && utf8 && app.settings.timeCustomFormat.size() < 64) {
      for (int i = 0; i < utf8Len; ++i) {
        const char c = utf8[i];
        if (c >= 32 && c < 127 && app.settings.timeCustomFormat.size() < 64)
          app.settings.timeCustomFormat.push_back(c);
      }
      draw(app);
    }
    return true;
  }
  return false;
}
