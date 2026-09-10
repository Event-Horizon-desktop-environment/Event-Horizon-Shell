#include "desktop_shell/widgets/world_clock/world_clock_paint.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"

#include "desktop_shell/widgets/shared/widget_settings.hpp"
#include "desktop_shell/widgets/shared/measure_scratch.hpp"
#include "configuration/shell_config.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <cairo.h>
#include <curl/curl.h>
#include <pango/pangocairo.h>

namespace eh::widgets {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

// API-fetched timezone cache.

struct ApiTimeData {
  long utcOffsetSeconds = 0;
  bool hasData = false;
};

static std::mutex g_apiMutex;
static std::unordered_map<std::string, ApiTimeData> g_apiCache;
static std::thread g_apiThread;
static std::atomic<bool> g_apiRunning{false};

size_t wc_curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  const size_t n = size * nmemb;
  out->append(ptr, n);
  return n;
}

bool wc_fetch_timezone(const std::string& tz, long& outOffsetSec) {
  std::string url = "https://timeapi.io/api/v1/timezone/zone?timeZone=";
  url += curl_easy_escape(nullptr, tz.c_str(), static_cast<int>(tz.size()));

  CURL* curl = curl_easy_init();
  if (!curl) return false;

  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wc_curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "EventHorizon/1.0");
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

  const CURLcode res = curl_easy_perform(curl);
  long http = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK || http != 200 || response.empty()) return false;

  // Simple JSON parsing to extract "currentUtcOffset" (integer)
  const std::string key = "\"currentUtcOffset\"";
  auto pos = response.find(key);
  if (pos == std::string::npos) {
    // Try the v1 API field name
    const std::string key2 = "\"current_utc_offset_seconds\"";
    pos = response.find(key2);
    if (pos == std::string::npos) return false;
    pos = response.find(':', pos + key2.size());
  } else {
    pos = response.find(':', pos + key.size());
  }
  if (pos == std::string::npos) return false;
  pos++; // skip ':'
  while (pos < response.size() && (response[pos] == ' ' || response[pos] == '\t')) pos++;
  char* end = nullptr;
  const long val = std::strtol(response.c_str() + pos, &end, 10);
  if (end == response.c_str() + pos) return false;
  outOffsetSec = val;
  return true;
}

void wc_api_thread_main(std::vector<std::string> timezones) {
  static bool curlInitDone = false;
  if (!curlInitDone) {
    curlInitDone = true;
    curl_global_init(CURL_GLOBAL_DEFAULT);
  }

  while (g_apiRunning) {
    for (const auto& tz : timezones) {
      if (!g_apiRunning) break;
      long offset = 0;
      if (wc_fetch_timezone(tz, offset)) {
        std::lock_guard<std::mutex> lock(g_apiMutex);
        g_apiCache[tz] = {offset, true};
      }
    }
    for (int i = 0; i < 30 * 60 && g_apiRunning; i++) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

void wc_ensure_api_fetch(const std::vector<std::string>& timezones) {
  if (timezones.empty()) return;
  static std::once_flag flag;
  std::call_once(flag, [&] {
    g_apiRunning = true;
    g_apiThread = std::thread(wc_api_thread_main, timezones);
    g_apiThread.detach();
  });
}

// City configuration.

struct CityEntry {
  std::string label;
  std::string timezone;
  // Legacy neon-accent fields, retained only so existing configs with
  // city_N_color entries keep parsing cleanly. Unused by the analog face
  // renderer below (face color is now driven by day/night, not per-city
  // accent).
  double glowR = 0.0;
  double glowG = 1.0;
  double glowB = 1.0;
};

static std::vector<CityEntry> parse_cities(const eh::config::ShellConfig& sc, std::string_view instance_id) {
  std::vector<CityEntry> cities;
  int idx = 1;
  while (true) {
    const std::string prefix = "city_" + std::to_string(idx);
    const std::string tz = widget_setting(sc, instance_id, prefix + "_tz");
    if (tz.empty()) break;
    std::string label = widget_setting(sc, instance_id, prefix + "_label");
    if (label.empty()) {
      auto pos = tz.find('/');
      label = (pos != std::string::npos) ? tz.substr(pos + 1) : tz;
      if (label.size() > 10) label = label.substr(0, 10);
    }
    const std::string colorKey = prefix + "_color";
    const std::string colorVal = widget_setting(sc, instance_id, colorKey);
    double r = 0.0, g = 1.0, b = 1.0;
    if (!colorVal.empty()) {
      unsigned int cr = 0, cg = 0, cb = 0;
      if (std::sscanf(colorVal.c_str(), "#%02x%02x%02x", &cr, &cg, &cb) == 3) {
        r = static_cast<double>(cr) / 255.0;
        g = static_cast<double>(cg) / 255.0;
        b = static_cast<double>(cb) / 255.0;
      }
    }
    cities.push_back({label, tz, r, g, b});
    idx++;
  }
  if (cities.empty()) {
    cities.push_back({"Local", "", 0.0, 1.0, 1.0});
    cities.push_back({"UTC", "UTC", 1.0, 0.0, 1.0});
  }

  // Start API fetch for configured timezones
  {
    std::vector<std::string> tzList;
    for (const auto& c : cities) {
      if (!c.timezone.empty()) tzList.push_back(c.timezone);
    }
    wc_ensure_api_fetch(tzList);
  }

  return cities;
}

// Time resolution.

struct CityTimeInfo {
  int hour = 0, minute = 0, second = 0;
  int year = 1970, month = 1, day = 1;
  long offsetSeconds = 0;
  bool offsetKnown = false;
};

// Days since 1970-01-01 for a civil (y, m, d) date. Standard algorithm
// (Howard Hinnant, public domain), timezone-agnostic.
long long days_from_civil(int y, unsigned m, unsigned d) {
  y -= (m <= 2) ? 1 : 0;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long long>(doe) - 719468;
}

CityTimeInfo resolve_city_time(const CityEntry& city, std::time_t now) {
  CityTimeInfo out;

  // Prefer the API-fetched offset when we have it.
  long offsetSec = 0;
  bool haveOffset = false;
  {
    std::lock_guard<std::mutex> lock(g_apiMutex);
    auto it = g_apiCache.find(city.timezone);
    if (it != g_apiCache.end() && it->second.hasData) {
      offsetSec = it->second.utcOffsetSeconds;
      haveOffset = true;
    }
  }

  std::tm tm{};
  if (haveOffset) {
    std::time_t localTs = now + offsetSec;
    gmtime_r(&localTs, &tm);
  } else if (city.timezone.empty()) {
    localtime_r(&now, &tm);
#if defined(__GLIBC__) || defined(__linux__)
    offsetSec = tm.tm_gmtoff;
    haveOffset = true;
#endif
  } else {
    // Thread-safe timezone lookup using std::chrono instead of setenv("TZ").
    const auto* tz = std::chrono::locate_zone(city.timezone);
    if (tz != nullptr) {
      const auto zt = std::chrono::zoned_time(tz, std::chrono::system_clock::from_time_t(now));
      const auto lt = zt.get_local_time();
      const auto tp = std::chrono::floor<std::chrono::seconds>(lt);
      const auto dp = std::chrono::floor<std::chrono::days>(tp);
      std::chrono::hh_mm_ss hms{tp - dp};
      std::chrono::year_month_day ymd{dp};
      tm.tm_year = static_cast<int>(ymd.year()) - 1900;
      tm.tm_mon = static_cast<unsigned>(ymd.month()) - 1;
      tm.tm_mday = static_cast<unsigned>(ymd.day());
      tm.tm_wday = static_cast<int>(std::chrono::weekday{dp}.c_encoding());
      tm.tm_hour = static_cast<int>(hms.hours().count());
      tm.tm_min = static_cast<int>(hms.minutes().count());
      tm.tm_sec = static_cast<int>(hms.seconds().count());
#if defined(__GLIBC__) || defined(__linux__)
      const auto offset = zt.get_info().offset;
      offsetSec = static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(offset).count());
      haveOffset = true;
#endif
    } else {
      // Unknown timezone — fall back to system local time
      localtime_r(&now, &tm);
#if defined(__GLIBC__) || defined(__linux__)
      offsetSec = tm.tm_gmtoff;
      haveOffset = true;
#endif
    }
  }

  out.hour = tm.tm_hour;
  out.minute = tm.tm_min;
  out.second = tm.tm_sec;
  out.year = tm.tm_year + 1900;
  out.month = tm.tm_mon + 1;
  out.day = tm.tm_mday;
  out.offsetSeconds = offsetSec;
  out.offsetKnown = haveOffset;
  return out;
}

std::string format_offset_label(const CityTimeInfo& city, const CityTimeInfo& reference) {
  if (!city.offsetKnown || !reference.offsetKnown) return "";
  const double diffHours = static_cast<double>(city.offsetSeconds - reference.offsetSeconds) / 3600.0;
  const double rounded = std::round(diffHours * 2.0) / 2.0;
  if (rounded == 0.0) return "Same Time";
  char buf[16];
  if (std::floor(rounded) == rounded) {
    std::snprintf(buf, sizeof(buf), "%+dHRS", static_cast<int>(rounded));
  } else {
    std::snprintf(buf, sizeof(buf), "%+.1fHRS", rounded);
  }
  return buf;
}

std::string format_day_label(const CityTimeInfo& city, const CityTimeInfo& reference) {
  const long long cityDay = days_from_civil(city.year, static_cast<unsigned>(city.month), static_cast<unsigned>(city.day));
  const long long refDay = days_from_civil(reference.year, static_cast<unsigned>(reference.month), static_cast<unsigned>(reference.day));
  const long long diff = cityDay - refDay;
  if (diff == 0) return "Today";
  if (diff == 1) return "Tomorrow";
  if (diff == -1) return "Yesterday";
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%+lld Days", diff);
  return buf;
}

// Apple-style day/night face: light dial in daylight hours, dark dial at night.
bool is_daytime(const CityTimeInfo& t) {
  return t.hour >= 7 && t.hour < 19;
}

// Pango helpers.

PangoLayout* wc_make_layout(cairo_t* cr, const char* font_desc_str) {
  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = pango_font_description_from_string(font_desc_str);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
  pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  return layout;
}

void draw_centered_text(cairo_t* cr, PangoLayout* layout, const std::string& text,
                        double centerX, double topY, double r, double g, double b, double alpha,
                        double* outHeight = nullptr) {
  pango_layout_set_text(layout, text.c_str(), -1);
  int tw = 0, th = 0;
  pango_layout_get_pixel_size(layout, &tw, &th);
  cairo_save(cr);
  cairo_set_source_rgba(cr, r, g, b, alpha);
  cairo_move_to(cr, centerX - tw / 2.0, topY);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  if (outHeight) *outHeight = th;
}

double measure_text_width(PangoLayout* layout, const std::string& text) {
  pango_layout_set_text(layout, text.c_str(), -1);
  int tw = 0, th = 0;
  pango_layout_get_pixel_size(layout, &tw, &th);
  return tw;
}

// Analog face drawing.

void draw_clock_face(cairo_t* cr, PangoLayout* numLayout, double cx, double cy, double radius, bool light) {
  cairo_save(cr);

  cairo_arc(cr, cx, cy, radius, 0, 2 * kPi);
  if (light) {
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  } else {
    cairo_set_source_rgb(cr, 0.173, 0.173, 0.180); // #2c2c2e
  }
  cairo_fill_preserve(cr);
  cairo_set_line_width(cr, 1.0);
  if (light) {
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.10);
  } else {
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.10);
  }
  cairo_stroke(cr);

  // Minor tick marks (skip positions that will carry a number).
  const double tickColor = light ? 0.0 : 1.0;
  for (int i = 0; i < 60; i++) {
    if (i % 5 == 0) continue;
    const double angle = i * 6.0 * kDegToRad;
    const double outerR = radius * 0.94;
    const double innerR = radius * 0.88;
    const double x1 = cx + outerR * std::sin(angle);
    const double y1 = cy - outerR * std::cos(angle);
    const double x2 = cx + innerR * std::sin(angle);
    const double y2 = cy - innerR * std::cos(angle);
    cairo_set_source_rgba(cr, tickColor, tickColor, tickColor, 0.35);
    cairo_set_line_width(cr, std::max(0.6, radius * 0.012));
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
  }

  // Hour numerals.
  for (int n = 1; n <= 12; n++) {
    const double angle = n * 30.0 * kDegToRad;
    const double r = radius * 0.74;
    const double x = cx + r * std::sin(angle);
    const double y = cy - r * std::cos(angle);
    pango_layout_set_text(numLayout, std::to_string(n).c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(numLayout, &tw, &th);
    cairo_set_source_rgb(cr, light ? 0.0 : 1.0, light ? 0.0 : 1.0, light ? 0.0 : 1.0);
    cairo_move_to(cr, x - tw / 2.0, y - th / 2.0);
    pango_cairo_show_layout(cr, numLayout);
  }

  cairo_restore(cr);
}

void draw_clock_hands(cairo_t* cr, double cx, double cy, double radius,
                      int hour24, int minute, int second, bool light) {
  const double hour12 = static_cast<double>(hour24 % 12);
  const double hourAngle = (hour12 * 30.0 + minute * 0.5) * kDegToRad;
  const double minuteAngle = (minute * 6.0 + second * 0.1) * kDegToRad;
  const double secondAngle = (second * 6.0) * kDegToRad;

  const double hourLen = radius * 0.50;
  const double minLen = radius * 0.72;
  const double secLen = radius * 0.80;
  const double secTail = radius * 0.16;

  const double handShade = light ? 0.0 : 1.0;

  cairo_save(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

  cairo_set_source_rgb(cr, handShade, handShade, handShade);
  cairo_set_line_width(cr, std::max(1.4, radius * 0.06));
  cairo_move_to(cr, cx, cy);
  cairo_line_to(cr, cx + hourLen * std::sin(hourAngle), cy - hourLen * std::cos(hourAngle));
  cairo_stroke(cr);

  cairo_set_line_width(cr, std::max(1.1, radius * 0.045));
  cairo_move_to(cr, cx, cy);
  cairo_line_to(cr, cx + minLen * std::sin(minuteAngle), cy - minLen * std::cos(minuteAngle));
  cairo_stroke(cr);

  // Orange second hand, with a short tail like the reference design.
  cairo_set_source_rgb(cr, 1.0, 0.624, 0.039); // #ff9f0a
  cairo_set_line_width(cr, std::max(0.9, radius * 0.022));
  cairo_move_to(cr, cx - secTail * std::sin(secondAngle), cy + secTail * std::cos(secondAngle));
  cairo_line_to(cr, cx + secLen * std::sin(secondAngle), cy - secLen * std::cos(secondAngle));
  cairo_stroke(cr);

  cairo_arc(cr, cx, cy, std::max(1.2, radius * 0.055), 0, 2 * kPi);
  cairo_set_source_rgb(cr, handShade, handShade, handShade);
  cairo_fill(cr);

  cairo_restore(cr);
}

// Shared layout metrics (kept identical between measure & paint).

struct WorldClockLayout {
  double diameter = 0.0;
  double cellW = 0.0;
  double fontCityPx = 0.0;
  double fontSmallPx = 0.0;
  double fontNumPx = 0.0;
  double labelBlockH = 0.0;
  double topPad = 0.0;
};

WorldClockLayout compute_layout(const std::vector<CityEntry>& cities,
                                double icon_ref_px, double slot_h, PangoLayout* cityLayout,
                                PangoLayout* smallLayout) {
  WorldClockLayout lay;
  lay.fontCityPx = std::clamp(icon_ref_px * 0.26, 10.0, 15.0);
  lay.fontSmallPx = std::clamp(icon_ref_px * 0.20, 8.0, 12.0);
  lay.fontNumPx = 0.0; // set per-clock, proportional to diameter

  const double cityLineH = lay.fontCityPx * 1.3;
  const double smallLineH = lay.fontSmallPx * 1.3;
  const double lineGap = 2.0;
  lay.labelBlockH = cityLineH + smallLineH + smallLineH + lineGap * 2.0;

  lay.topPad = std::max(6.0, slot_h * 0.06);
  const double bottomPad = std::max(6.0, slot_h * 0.05);
  const double clockToLabelGap = 6.0;

  lay.diameter = std::clamp(slot_h - lay.topPad - clockToLabelGap - lay.labelBlockH - bottomPad,
                            24.0, 120.0);

  double maxLabelW = 0.0;
  for (const auto& city : cities) {
    maxLabelW = std::max(maxLabelW, measure_text_width(cityLayout, city.label));
  }
  maxLabelW = std::max(maxLabelW, measure_text_width(smallLayout, "Tomorrow"));
  maxLabelW = std::max(maxLabelW, measure_text_width(smallLayout, "+16HRS"));

  lay.cellW = std::max(lay.diameter + 12.0, maxLabelW + 10.0);
  return lay;
}

}  // namespace

bool world_clock_tick_signature_changed(int& cached_signature, const eh::config::ShellConfig& sc,
                                         std::string_view instance_id) {
  (void)sc; (void)instance_id;
  const std::time_t now = std::time(nullptr);
  const int sig = static_cast<int>(now); // seconds hand is always visible now, tick every second
  if (sig == cached_signature) return false;
  cached_signature = sig;
  return true;
}

double dock_world_clock_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc,
                                    std::string_view instance_id, double icon_ref_px, double bar_height) {
  if (measure_cr == nullptr) measure_cr = get_measure_cr();

  const auto cities = parse_cities(sc, instance_id);
  const double slot_h = std::max(bar_height, icon_ref_px * 3.0);

  std::ostringstream cityFd, smallFd;
  const double fontCityPx = std::clamp(icon_ref_px * 0.26, 10.0, 15.0);
  const double fontSmallPx = std::clamp(icon_ref_px * 0.20, 8.0, 12.0);
  cityFd << "Sans Bold " << static_cast<int>(fontCityPx);
  smallFd << "Sans " << static_cast<int>(fontSmallPx);

  PangoLayout* cityLayout = wc_make_layout(measure_cr, cityFd.str().c_str());
  PangoLayout* smallLayout = wc_make_layout(measure_cr, smallFd.str().c_str());

  const WorldClockLayout lay = compute_layout(cities, icon_ref_px, slot_h, cityLayout, smallLayout);

  g_object_unref(cityLayout);
  g_object_unref(smallLayout);

  const double interCellGap = 18.0;
  const double outerPad = 16.0;
  const size_t n = std::max<size_t>(cities.size(), 1);
  const double inner = lay.cellW * static_cast<double>(n) + interCellGap * static_cast<double>(n - 1) + outerPad * 2.0;

  const double minW = std::max(icon_ref_px * 1.5, 64.0);
  const double maxCap = std::min(bar_height * 12.0, 640.0);
  return std::clamp(inner, minW, maxCap);
}

void paint_world_clock_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id, double x, double y,
                             double slot_w, double slot_h, double icon_ref_px, bool hovered, bool pressed) {
  (void)hovered; (void)pressed;

  const auto cities = parse_cities(sc, instance_id);
  if (cities.empty()) return;

  const std::time_t now = std::time(nullptr);
  const CityEntry refEntry{"", "", 0, 0, 0}; // empty timezone -> device local time
  const CityTimeInfo reference = resolve_city_time(refEntry, now);

  cairo_save(cr);
  slot_pill_style::paint_pill(cr, x, y, slot_w, slot_h);

  std::ostringstream cityFd, smallFd, numFd;
  const double fontCityPx = std::clamp(icon_ref_px * 0.26, 10.0, 15.0);
  const double fontSmallPx = std::clamp(icon_ref_px * 0.20, 8.0, 12.0);
  cityFd << "Sans Bold " << static_cast<int>(fontCityPx);
  smallFd << "Sans " << static_cast<int>(fontSmallPx);

  PangoLayout* cityLayout = wc_make_layout(cr, cityFd.str().c_str());
  PangoLayout* smallLayout = wc_make_layout(cr, smallFd.str().c_str());

  const WorldClockLayout lay = compute_layout(cities, icon_ref_px, slot_h, cityLayout, smallLayout);

  const double numFontPx = std::clamp(lay.diameter * 0.16, 7.0, 13.0);
  numFd << "Sans Bold " << static_cast<int>(numFontPx);
  PangoLayout* numLayout = wc_make_layout(cr, numFd.str().c_str());

  const double interCellGap = 18.0;
  const double outerPad = 16.0;
  const double totalContentW = lay.cellW * static_cast<double>(cities.size())
                              + interCellGap * static_cast<double>(cities.size() > 0 ? cities.size() - 1 : 0);
  double cursorX = x + (slot_w - totalContentW) / 2.0;
  if (cursorX < x + outerPad * 0.5) cursorX = x + outerPad * 0.5;

  const double clockCy = y + lay.topPad + lay.diameter / 2.0;
  const double clockToLabelGap = 6.0;
  double labelTopY = clockCy + lay.diameter / 2.0 + clockToLabelGap;

  for (const auto& city : cities) {
    const double cellCx = cursorX + lay.cellW / 2.0;

    const CityTimeInfo t = resolve_city_time(city, now);
    const bool light = is_daytime(t);

    draw_clock_face(cr, numLayout, cellCx, clockCy, lay.diameter / 2.0, light);
    draw_clock_hands(cr, cellCx, clockCy, lay.diameter / 2.0, t.hour, t.minute, t.second, light);

    double h1 = 0, h2 = 0, h3 = 0;
    double ty = labelTopY;
    draw_centered_text(cr, cityLayout, city.label, cellCx, ty, 1.0, 1.0, 1.0, 1.0, &h1);
    ty += h1 + 2.0;
    draw_centered_text(cr, smallLayout, format_day_label(t, reference), cellCx, ty, 0.60, 0.60, 0.62, 1.0, &h2);
    ty += h2 + 1.0;
    draw_centered_text(cr, smallLayout, format_offset_label(t, reference), cellCx, ty, 0.60, 0.60, 0.62, 1.0, &h3);

    cursorX += lay.cellW + interCellGap;
  }

  g_object_unref(cityLayout);
  g_object_unref(smallLayout);
  g_object_unref(numLayout);
  cairo_restore(cr);
}

}  // namespace eh::widgets
