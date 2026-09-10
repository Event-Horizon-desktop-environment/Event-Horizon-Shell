#include "desktop_shell/widgets/clock/clock_paint.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"

#include "desktop_shell/widgets/shared/widget_settings.hpp"
#include "desktop_shell/widgets/shared/measure_scratch.hpp"
#include "configuration/shell_config.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include <cairo.h>
#include <pango/pangocairo.h>

namespace eh::widgets {
namespace {

// Thread-safe timezone helper: computes broken-down local time for an IANA
// timezone without mutating the process-global TZ environment variable.
// Falls back to system localtime when tzName is empty.
struct SafeLocalTime {
  std::tm tm{};
  long utcOffsetSeconds = 0;
};

SafeLocalTime safe_localtime(std::time_t now, const std::string& tzName) {
  SafeLocalTime out{};
  if (tzName.empty()) {
    if (localtime_r(&now, &out.tm) != nullptr) {
#if defined(__GLIBC__) || defined(__linux__)
      out.utcOffsetSeconds = out.tm.tm_gmtoff;
#endif
    }
    return out;
  }
  const auto* tz = std::chrono::locate_zone(tzName);
  if (tz == nullptr) {
    if (localtime_r(&now, &out.tm) != nullptr) {
#if defined(__GLIBC__) || defined(__linux__)
      out.utcOffsetSeconds = out.tm.tm_gmtoff;
#endif
    }
    return out;
  }
  const auto zt = std::chrono::zoned_time(tz, std::chrono::system_clock::from_time_t(now));
  const auto lt = zt.get_local_time();
  const auto tp = std::chrono::floor<std::chrono::seconds>(lt);
  const auto dp = std::chrono::floor<std::chrono::days>(tp);
  std::chrono::hh_mm_ss hms{tp - dp};
  std::chrono::year_month_day ymd{dp};
  out.tm.tm_year = static_cast<int>(ymd.year()) - 1900;
  out.tm.tm_mon = static_cast<unsigned>(ymd.month()) - 1;
  out.tm.tm_mday = static_cast<unsigned>(ymd.day());
  out.tm.tm_wday = static_cast<int>(std::chrono::weekday{dp}.c_encoding());
  const auto jan1 = std::chrono::sys_days(std::chrono::year_month_day{
      ymd.year(), std::chrono::month{1}, std::chrono::day{1}});
  out.tm.tm_yday = static_cast<int>((std::chrono::sys_days(ymd) - jan1).count());
  out.tm.tm_hour = static_cast<int>(hms.hours().count());
  out.tm.tm_min = static_cast<int>(hms.minutes().count());
  out.tm.tm_sec = static_cast<int>(hms.seconds().count());
  const auto offset = zt.get_info().offset;
  out.utcOffsetSeconds = static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(offset).count());
  return out;
}

// Per-instance cache to avoid recreating PangoLayout every frame
// when the text content and font size haven't changed.
struct ClockLayoutCache {
  double lastFontPx = 0;
  std::string cacheKey;
  std::vector<int> lineHeights;
  std::vector<int> lineWidths;
  int totalTextH = 0;
  int maxWidth = 0;
  PangoLayout* layout = nullptr;
  ~ClockLayoutCache() { if (layout) g_object_unref(layout); }
  ClockLayoutCache() = default;
  ClockLayoutCache(ClockLayoutCache&& o) noexcept
      : lastFontPx(o.lastFontPx), cacheKey(std::move(o.cacheKey)),
        lineHeights(std::move(o.lineHeights)), lineWidths(std::move(o.lineWidths)),
        totalTextH(o.totalTextH), maxWidth(o.maxWidth), layout(o.layout) {
    o.layout = nullptr;
  }
  ClockLayoutCache& operator=(ClockLayoutCache&& o) noexcept {
    if (layout) g_object_unref(layout);
    lastFontPx = o.lastFontPx;
    cacheKey = std::move(o.cacheKey);
    lineHeights = std::move(o.lineHeights);
    lineWidths = std::move(o.lineWidths);
    totalTextH = o.totalTextH;
    maxWidth = o.maxWidth;
    layout = o.layout;
    o.layout = nullptr;
    return *this;
  }
  ClockLayoutCache(const ClockLayoutCache&) = delete;
  ClockLayoutCache& operator=(const ClockLayoutCache&) = delete;
};

static std::unordered_map<std::string, ClockLayoutCache> g_clockLayoutCache;

bool parse_bool_setting(const std::string& v, bool fallback) {
   
  if (v.empty()) return fallback;
  if (v == "1" || v == "true" || v == "True" || v == "yes" || v == "Yes") return true;
  if (v == "0" || v == "false" || v == "False" || v == "no" || v == "No") return false;
  return fallback;
}

static void append_weekday_short(std::string& s, const std::tm& tm) {
   
  static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  if (tm.tm_wday >= 0 && tm.tm_wday <= 6) s += kDays[tm.tm_wday];
}

static void append_date_formatted(std::string& s, const std::tm& tm, int dateFormat) {
   
  switch (dateFormat) {
    case 1: {
      static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
      append_weekday_short(s, tm);
      s += ' ';
      s += std::to_string(tm.tm_mday);
      s += ' ';
      if (tm.tm_mon >= 0 && tm.tm_mon <= 11) s += kMonths[tm.tm_mon];
      s += ' ';
      s += std::to_string(tm.tm_year + 1900);
      break;
    }
    case 2: {
      char buf[32];
      if (std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm) > 0) s += buf;
      break;
    }
    default: {
      append_weekday_short(s, tm);
      s += ' ';
      s += std::to_string(tm.tm_mday);
      break;
    }
  }
}

void format_clock_lines(const eh::config::ShellConfig& sc, std::string_view instance_id, std::vector<std::string>& lines) {
    
  const bool showSeconds = parse_bool_setting(widget_setting(sc, instance_id, "show_seconds"), sc.time.showSeconds);
  const std::time_t now = std::time(nullptr);
  const int granularity = showSeconds ? 1 : 60;
  const std::time_t tickKey = now / granularity;

  static std::unordered_map<std::string, std::vector<std::string>> s_cache;
  const std::string ck = std::string(instance_id) + ':' + std::to_string(tickKey);
  auto it = s_cache.find(ck);
  if (it != s_cache.end()) {
    lines = it->second;
    return;
  }

  lines.clear();

  std::string custom = widget_setting(sc, instance_id, "format");
  {
    constexpr const char* kStd24 = "%H:%M";
    constexpr const char* kStd24s = "%H:%M:%S";
    constexpr const char* kStd12 = "%I:%M %p";
    constexpr const char* kStd12s = "%I:%M:%S %p";
    const bool isStdFmt = custom == kStd24 || custom == kStd24s || custom == kStd12 || custom == kStd12s;
    if (isStdFmt) custom.clear();
  }
  if (custom.empty()) custom = sc.time.customFormat;

  const bool use24 = parse_bool_setting(widget_setting(sc, instance_id, "use_24h"), sc.time.use24h);
  const bool compact = parse_bool_setting(widget_setting(sc, instance_id, "compact"), false);
  const bool showDate = parse_bool_setting(widget_setting(sc, instance_id, "show_date"), sc.time.showDate);
  const int dateFormat = [&]() -> int {
    const std::string df = widget_setting(sc, instance_id, "date_format");
    if (!df.empty()) {
      char* end = nullptr;
      const long v = std::strtol(df.c_str(), &end, 10);
      if (end != df.c_str()) return static_cast<int>(std::clamp(v, 0L, 3L));
    }
    return sc.time.dateFormat;
  }();

  std::tm local{};
  {
    const std::string& tz = sc.time.timezone;
    const auto slt = safe_localtime(now, tz);
    local = slt.tm;
    if (local.tm_hour == 0 && local.tm_min == 0 && local.tm_sec == 0 && now != 0) return;
  }

  if (!custom.empty()) {
    char buf[128];
    if (std::strftime(buf, sizeof(buf), custom.c_str(), &local) > 0) lines.push_back(buf);
    s_cache[ck] = lines;
    return;
  }

  if (use24) {
    char timebuf[32];
    if (showSeconds) {
      if (std::strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &local) == 0) return;
    } else {
      if (std::strftime(timebuf, sizeof(timebuf), "%H:%M", &local) == 0) return;
    }
    if (showDate && !compact) {
      std::string combined(timebuf);
      combined += ' ';
      append_date_formatted(combined, local, dateFormat);
      lines.push_back(std::move(combined));
    } else {
      lines.push_back(timebuf);
    }
    s_cache[ck] = lines;
    return;
  }

  int h12 = local.tm_hour % 12;
  if (h12 == 0) h12 = 12;
  const char* ampm = (local.tm_hour < 12) ? "AM" : "PM";
  char timebuf[32];
  if (showSeconds) {
    std::snprintf(timebuf, sizeof(timebuf), "%d:%02d:%02d %s", h12, local.tm_min, local.tm_sec, ampm);
  } else {
    std::snprintf(timebuf, sizeof(timebuf), "%d:%02d %s", h12, local.tm_min, ampm);
  }
  if (compact || !showDate) {
    lines.push_back(timebuf);
    s_cache[ck] = lines;
    return;
  }
  std::string date;
  date.reserve(48);
  append_date_formatted(date, local, dateFormat);
  lines.push_back(std::string(timebuf) + " \xC2\xB7 " + date);
  s_cache[ck] = lines;
}

PangoLayout* make_layout(cairo_t* cr, const char* font_desc_str) {
   
  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = pango_font_description_from_string(font_desc_str);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
  pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  return layout;
}

}

bool widget_list_contains_clock(const eh::config::ShellConfig& sc, const std::vector<std::string>& widgets) {
   
  (void)sc;
  for (const auto& id : widgets) {
    if (eh::config::widget_implementation_type(id) == "clock") return true;
  }
  return false;
}

bool clock_tick_signature_changed(int& cached_signature, const eh::config::ShellConfig& sc, std::string_view instance_id) {
   
  const bool showSeconds = parse_bool_setting(widget_setting(sc, instance_id, "show_seconds"), sc.time.showSeconds);
  std::time_t t = std::time(nullptr);
  std::tm local{};
  {
    const std::string& tz = sc.time.timezone;
    const auto slt = safe_localtime(t, tz);
    local = slt.tm;
    if (local.tm_hour == 0 && local.tm_min == 0 && local.tm_sec == 0 && t != 0) return false;
  }
  const int sig =
      showSeconds ? (local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec) : (local.tm_hour * 60 + local.tm_min);
  if (sig == cached_signature) return false;
  cached_signature = sig;
  return true;
}

static std::string build_cache_key(const std::vector<std::string>& lines) {
  std::string key;
  for (const auto& ln : lines) {
    key += ln;
    key += '\n';
  }
  return key;
}

double dock_clock_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc, std::string_view instance_id,
                             double icon_ref_px, double bar_height) {
   
  if (measure_cr == nullptr) {
    measure_cr = get_measure_cr();
  }

  std::vector<std::string> lines;
  format_clock_lines(sc, instance_id, lines);
  if (lines.empty()) return std::max(56.0, icon_ref_px * 2.4);

  const double fontPx = std::clamp(icon_ref_px * 0.38, 11.0, 15.0);
  std::ostringstream fd;
  fd << "Inter " << static_cast<int>(fontPx);
  PangoLayout* layout = make_layout(measure_cr, fd.str().c_str());

  int textMaxW = 0;
  for (const auto& ln : lines) {
    pango_layout_set_text(layout, ln.c_str(), -1);
    int w = 0;
    int h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    textMaxW = std::max(textMaxW, w);
  }
  g_object_unref(layout);

  const double padX = 10.0;
  const double minW = std::max(icon_ref_px * 1.25, 52.0);
  const double maxCap = std::min(bar_height * 6.0, 220.0);
  const double inner = static_cast<double>(textMaxW) + padX * 2.0;
  return std::clamp(inner, minW, maxCap);
}

void paint_clock_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id, double x, double y,
                      double slot_w, double slot_h, double icon_ref_px, bool hovered, bool pressed) {
  (void)hovered; (void)pressed;
  std::vector<std::string> lines;
  format_clock_lines(sc, instance_id, lines);
  if (lines.empty()) return;

  cairo_save(cr);
  slot_pill_style::paint_pill(cr, x, y, slot_w, slot_h);

  const double fontPx = std::clamp(icon_ref_px * 0.38, 11.0, 15.0);
  std::ostringstream fd;
  fd << "Inter " << static_cast<int>(fontPx);
  const std::string fdStr = fd.str();

  const std::string kid(instance_id);
  auto it = g_clockLayoutCache.find(kid);
  std::string ck = build_cache_key(lines);
  bool cacheHit = (it != g_clockLayoutCache.end() && it->second.cacheKey == ck && it->second.lastFontPx == fontPx);
  ClockLayoutCache* clp = cacheHit ? &it->second : &g_clockLayoutCache[kid];
  ClockLayoutCache& clc = *clp;
  if (!cacheHit) {
    clc.cacheKey = ck;
    clc.lastFontPx = fontPx;
    clc.lineHeights.clear();
    clc.lineWidths.clear();
    clc.maxWidth = 0;
    clc.totalTextH = 0;
  }
  if (!clc.layout || clc.lastFontPx != fontPx) {
    if (clc.layout) g_object_unref(clc.layout);
    clc.layout = make_layout(cr, fdStr.c_str());
  } else {
    pango_cairo_update_layout(cr, clc.layout);
  }
  PangoLayout* layout = clc.layout;
  pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);

  int totalTextH = 0;
  if (cacheHit) {
    totalTextH = clc.totalTextH;
  } else {
    for (const auto& ln : lines) {
      pango_layout_set_text(layout, ln.c_str(), -1);
      int w = 0, h = 0;
      pango_layout_get_pixel_size(layout, &w, &h);
      clc.lineHeights.push_back(h);
      clc.lineWidths.push_back(w);
      clc.totalTextH += h;
      clc.maxWidth = std::max(clc.maxWidth, w);
    }
    if (lines.size() > 1) clc.totalTextH += static_cast<int>((lines.size() - 1) * 2);
    totalTextH = clc.totalTextH;
  }

  double ty = y + (slot_h - static_cast<double>(totalTextH)) / 2.0;
  for (size_t i = 0; i < lines.size(); i++) {
    const std::string& ln = lines[i];
    pango_layout_set_text(layout, ln.c_str(), -1);
    if (i == 0 && lines.size() > 1) {
      PangoAttrList* attrs = pango_attr_list_new();
      PangoAttribute* semi = pango_attr_weight_new(PANGO_WEIGHT_SEMIBOLD);
      pango_attr_list_insert(attrs, semi);
      pango_layout_set_attributes(layout, attrs);
      pango_attr_list_unref(attrs);
    } else {
      pango_layout_set_attributes(layout, nullptr);
    }
    int lw = 0;
    int h = 0;
    if (cacheHit) {
      lw = it->second.lineWidths[i];
      h = it->second.lineHeights[i];
    } else {
      pango_layout_get_pixel_size(layout, &lw, &h);
    }
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_move_to(cr, x + (slot_w - static_cast<double>(lw)) * 0.5, ty);
    pango_cairo_show_layout(cr, layout);
    ty += static_cast<double>(h + (i + 1 < lines.size() ? 2 : 0));
  }
  cairo_restore(cr);
}

}
