#include "desktop_shell/dashboard/dashboard_layout.hpp"

#include "desktop_shell/controlcenter/layout/control_center_layout.hpp"
#include "desktop_shell/controlcenter/layout/control_center_slider_metrics.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/spotlight/paint/spotlight_paint.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "configuration/shell_config.hpp"

#include "desktop_shell/common/time/mono_time.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace eh::shell::dashboard {

std::vector<eh::shell::dock::control_center::ControlCenterWifiAp> dashboard_sorted_wifi_aps() {
  auto aps = eh::shell::dock_slot_hooks::control_center_wifi_scan(false);
  std::stable_sort(aps.begin(), aps.end(), [](const auto& a, const auto& b) {
    const int ka = a.active ? 0 : 1;
    const int kb = b.active ? 0 : 1;
    if (ka != kb) return ka < kb;
    return a.signal_pct > b.signal_pct;
  });
  return aps;
}

std::vector<eh::widgets::BluetoothDevice> dashboard_sorted_bt_devices() {
  auto devs = eh::shell::dock_slot_hooks::bluetooth_devices();
  std::stable_sort(devs.begin(), devs.end(), [](const auto& a, const auto& b) {
    const int ka = a.connected ? 0 : (a.paired ? 1 : 2);
    const int kb = b.connected ? 0 : (b.paired ? 1 : 2);
    if (ka != kb) return ka < kb;
    return a.alias < b.alias;
  });
  return devs;
}

DashMediaProgressGeom dashboard_media_progress_geom(const CardRect& c) {
  DashMediaProgressGeom g;
  const double barRight = c.x + c.w - 132.0;
  const double barX = c.x + 78.0;
  const double cy = dashboard_media_row_cy(c);
  g.trackX = barX;
  g.trackW = std::max(30.0, barRight - barX);
  g.trackY = cy + 10.0;
  g.trackH = 3.0;
  g.hitX = barX;
  g.hitW = g.trackW;
  g.hitY = cy + 2.0;
  g.hitH = 20.0;
  return g;
}

double dashboard_media_row_cy(const CardRect& c) { return c.y + c.h * 0.5; }

double dashboard_media_btn_cx(const CardRect& c, int idx) {
  if (idx <= 0) return c.x + c.w - 110.0;
  if (idx == 1) return c.x + c.w - 78.0;
  return c.x + c.w - 46.0;
}

DashAudioGeom dashboard_audio_geom(const CardRect& c) {
  DashAudioGeom g;
  const double cy = c.y + 52.0;
  g.muteX = c.x + 8.0;
  g.muteY = cy - 14.0;
  g.muteS = 28.0;
  g.iconCX = c.x + 22.0;
  g.iconCY = cy;
  g.trackX = c.x + 44.0;
  g.trackW = std::max(30.0, c.w - 100.0);
  g.trackY = cy - 1.5;
  g.trackH = 3.0;
  g.hitX = g.trackX;
  g.hitW = g.trackW;
  g.hitY = cy - 10.0;
  g.hitH = 20.0;
  g.pctRightX = c.x + c.w - 16.0;
  g.pctBase = cy + 4.0;
  return g;
}

namespace {

namespace cc = eh::shell::dock::control_center;
namespace sm = eh::shell::cc_slider;

// Panel frame between the glass backdrop and the card grid (v5: 20px).
// Inter-card spacing is the `gap` config (v5 tiles: 12px).
constexpr double kPad = 20.0;

constexpr double kNetHeaderH = 40.0;   // dashboard compact header (CC's own is 76)
constexpr double kRowHeaderH = 32.0;
constexpr double kRowH = 34.0;
constexpr double kRowBottomPad = 12.0;
constexpr double kWifiErrorH = 18.0;
constexpr double kMixerHeaderH = sm::kMixerHeaderH;
constexpr double kMixerRowH = sm::kMixerRowH;

int row_count(int n) { return std::max(1, std::min(6, n)); }

bool wifi_error_active(const DockApp& app) {
  return !app.dash.wifiError.empty() && eh::shell::now_mono_ms() < app.dash.wifiErrorUntilMs;
}

double natural_height(DockApp& app, DashCardKind k, int span) {
  switch (k) {
    case DashCardKind::Clock:
      return 104.0;
    case DashCardKind::Calendar:
      return 108.0;  // current-week strip
    case DashCardKind::Weather:
      return 100.0;
    case DashCardKind::Media:
      return 84.0;
    case DashCardKind::Volume:
    case DashCardKind::Mic:
      return 84.0;
    case DashCardKind::Network: {
      double h = kNetHeaderH;
      if (app.dash.netExpanded) {
        const auto aps = dashboard_sorted_wifi_aps();
        h += kRowHeaderH + static_cast<double>(row_count(static_cast<int>(aps.size()))) * kRowH + kRowBottomPad;
        if (wifi_error_active(app)) h += kWifiErrorH;
      }
      return h;
    }
    case DashCardKind::Bluetooth: {
      double h = kNetHeaderH;
      if (app.dash.btExpanded) {
        const auto devs = dashboard_sorted_bt_devices();
        h += kRowHeaderH + static_cast<double>(row_count(static_cast<int>(devs.size()))) * kRowH + kRowBottomPad;
      }
      return h;
    }
    case DashCardKind::Mixer: {
      const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
      if (streams.empty()) return 92.0;
      const int n = std::min(static_cast<int>(streams.size()), span >= 2 ? 6 : 3);
      return kMixerHeaderH + static_cast<double>(n) * kMixerRowH + 14.0;
    }
    case DashCardKind::System:
      return 100.0;
    default:
      return kNetHeaderH;
  }
}

void push_sub(CardRect& c, DashCardRole role, double px, double py, double pw, double ph, int row = -1,
              int streamId = -1, double trackX = 0.0, double trackW = 0.0, double hitPadH = 0.0) {
  SubRect s;
  s.role = role;
  s.row = row;
  s.streamId = streamId;
  s.paintX = px;
  s.paintY = py;
  s.paintW = pw;
  s.paintH = ph;
  s.hitX = px - hitPadH;
  s.hitY = py;
  s.hitW = pw + 2.0 * hitPadH;
  s.hitH = ph;
  s.trackX = trackX;
  s.trackW = trackW;
  c.subs.push_back(s);
}

void build_subs(CardRect& c, DockApp& app) {
  c.subs.clear();
  switch (c.kind) {
    case DashCardKind::Volume:
    case DashCardKind::Mic: {
      // Mute first: its box overlaps the slider's padded hit box.
      const DashAudioGeom g = dashboard_audio_geom(c);
      push_sub(c, DashCardRole::Mute, g.muteX, g.muteY, g.muteS, g.muteS);
      push_sub(c, DashCardRole::Slider, g.hitX, g.hitY, g.hitW, g.hitH, -1, -1, g.trackX, g.trackW);
      break;
    }
    case DashCardKind::Network: {
      push_sub(c, DashCardRole::Toggle, c.x, c.y, c.w, kNetHeaderH);
      if (app.dash.netExpanded) {
        const auto aps = dashboard_sorted_wifi_aps();
        const int rows = std::min(row_count(static_cast<int>(aps.size())), static_cast<int>(aps.size()));
        for (int i = 0; i < rows; ++i)
          push_sub(c, DashCardRole::Row, c.x, cc::cc_panel_row_y(c.y + kNetHeaderH, i), c.w, kRowH, i);
      }
      break;
    }
    case DashCardKind::Bluetooth: {
      push_sub(c, DashCardRole::Toggle, c.x, c.y, c.w, kNetHeaderH);
      if (app.dash.btExpanded) {
        const auto devs = dashboard_sorted_bt_devices();
        const int rows = std::min(row_count(static_cast<int>(devs.size())), static_cast<int>(devs.size()));
        for (int i = 0; i < rows; ++i)
          push_sub(c, DashCardRole::Row, c.x, cc::cc_panel_row_y(c.y + kNetHeaderH, i), c.w, kRowH, i);
      }
      break;
    }
    case DashCardKind::Media: {
      const double cy = dashboard_media_row_cy(c);
      push_sub(c, DashCardRole::MediaPrev, dashboard_media_btn_cx(c, 0) - 14.0, cy - 14.0, 28.0, 28.0);
      push_sub(c, DashCardRole::MediaPlayPause, dashboard_media_btn_cx(c, 1) - 14.0, cy - 14.0, 28.0, 28.0);
      push_sub(c, DashCardRole::MediaNext, dashboard_media_btn_cx(c, 2) - 14.0, cy - 14.0, 28.0, 28.0);
      bool showBar = false;
      if (app.mpris) {
        const auto ms = app.mpris->snapshot();
        showBar = ms.active && (!ms.title.empty() || !ms.artist.empty()) && ms.duration_us > 0;
      }
      if (showBar) {
        const DashMediaProgressGeom g = dashboard_media_progress_geom(c);
        push_sub(c, DashCardRole::Slider, g.hitX, g.hitY, g.hitW, g.hitH, -1, -1, g.trackX, g.trackW);
      }
      break;
    }
    case DashCardKind::Mixer: {
      const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
      const int n = std::min(static_cast<int>(streams.size()), c.span >= 2 ? 6 : 3);
      const DashMixerGeom g = dashboard_mixer_geom(c.x, c.w);
      for (int i = 0; i < n; ++i) {
        const double rowY = c.y + kMixerHeaderH + static_cast<double>(i) * kMixerRowH;
        const double sliderCY = rowY + sm::kMixerSliderCY;
        const int sid = streams[static_cast<size_t>(i)].sink_input_id;
        push_sub(c, DashCardRole::Slider, g.trackX, sliderCY - 19.0, g.trackW, 38.0, i, sid, g.trackX, g.trackW,
                 sm::kMixerHitPadH);
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace

DashCardKind dash_kind_from_id(std::string_view id) {
  if (id == "clock") return DashCardKind::Clock;
  if (id == "calendar") return DashCardKind::Calendar;
  if (id == "weather") return DashCardKind::Weather;
  if (id == "media") return DashCardKind::Media;
  if (id == "volume") return DashCardKind::Volume;
  if (id == "mic") return DashCardKind::Mic;
  if (id == "network") return DashCardKind::Network;
  if (id == "bluetooth") return DashCardKind::Bluetooth;
  if (id == "mixer") return DashCardKind::Mixer;
  if (id == "system") return DashCardKind::System;
  return DashCardKind::None;
}

bool dashboard_has_card(const DockApp& app, std::string_view id) {
  const eh::config::DashboardConfig cfg =
      app.dash.cfgValid ? app.dash.cfg : eh::config::shell_config_snapshot().dashboard;
  for (const auto& c : cfg.cards)
    if (c.id == id) return true;
  return false;
}

DashMixerGeom dashboard_mixer_geom(double cardX, double cardW) {
  DashMixerGeom g;
  g.nameX = cardX + 40.0;
  const double trackRight = cardX + cardW - 10.0;
  double trackW = std::max(60.0, cardW * 0.45);
  if (trackW > cardW - 60.0) trackW = std::max(30.0, cardW - 60.0);
  double trackX = trackRight - trackW;
  if (trackX < g.nameX + 20.0) {
    trackX = g.nameX + 20.0;
    trackW = std::max(30.0, trackRight - trackX);
  }
  g.trackX = trackX;
  g.trackW = trackW;
  g.nameBudget = std::max(20.0, trackX - 10.0 - g.nameX);
  return g;
}

DashboardLayout dashboard_compute_layout(DockApp& app, double surfaceW) {
  DashboardLayout L;
  L.surfaceW = surfaceW;

  const eh::config::DashboardConfig cfg =
      app.dash.cfgValid ? app.dash.cfg : eh::config::shell_config_snapshot().dashboard;
  if (surfaceW <= 1.0) return L;

  if (cfg.cards.empty()) return L;

  const int cols = std::clamp(cfg.columns, 1, 8);
  const double gap = std::clamp(static_cast<double>(cfg.gap), 0.0, 48.0);
  const double marginX = std::clamp(static_cast<double>(cfg.marginX), 0.0, 200.0);
  const double marginTop = std::clamp(static_cast<double>(cfg.marginTop), 0.0, 200.0);
  const double marginBottom = std::clamp(static_cast<double>(cfg.marginBottom), 0.0, 200.0);

  const double avail = std::max(80.0, surfaceW - 2.0 * marginX);
  const double panelW = cfg.maxWidth > 0 ? std::min(static_cast<double>(cfg.maxWidth), avail) : avail;
  const double panelX = (surfaceW - panelW) * 0.5;
  const double gridW = std::max(40.0, panelW - 2.0 * kPad);
  double cell = (gridW - static_cast<double>(cols - 1) * gap) / static_cast<double>(cols);
  cell = std::max(40.0, cell);

  L.panelX = panelX;
  L.panelY = marginTop;

  double rowTop = L.panelY + kPad;
  double rowH = 0.0;
  int used = 0;
  bool any = false;
  // Every card in a row shares the row height (tallest natural height), so no
  // bare panel band shows between a short card and the row below. Subs are
  // built once the final height is known (media buttons center on it).
  std::vector<CardRect> rowCards;
  auto flush_row = [&]() {
    for (auto& rc : rowCards) {
      rc.h = rowH;
      build_subs(rc, app);
      L.cards.push_back(std::move(rc));
    }
    rowCards.clear();
    rowH = 0.0;
  };

  for (const auto& card : cfg.cards) {
    const DashCardKind kind = dash_kind_from_id(card.id);
    if (kind == DashCardKind::None) continue;
    int span = std::clamp(card.span, 1, cols);
    if (used > 0 && used + span > cols) {
      rowTop += rowH + gap;
      used = 0;
      flush_row();
    }
    const double w = static_cast<double>(span) * cell + static_cast<double>(span - 1) * gap;
    const double h = natural_height(app, kind, span);

    CardRect c;
    c.id = card.id;
    c.kind = kind;
    c.span = span;
    c.x = panelX + kPad + static_cast<double>(used) * (cell + gap);
    c.y = rowTop;
    c.w = w;
    c.h = h;
    rowCards.push_back(std::move(c));

    used += span;
    rowH = std::max(rowH, h);
    any = true;
  }

  if (!any) return L;

  const double gridH = (rowTop - (L.panelY + kPad)) + rowH;
  L.panelH = gridH + 2.0 * kPad;
  L.surfaceH = marginTop + L.panelH + marginBottom;
  flush_row();
  L.valid = true;
  return L;
}

double dashboard_desired_height(DockApp& app, double surfaceW) {
  const DashboardLayout L = dashboard_compute_layout(app, surfaceW);
  return L.valid ? L.surfaceH : 0.0;
}

namespace {

// Thread-safe timezone helper: broken-down local time for an IANA zone without
// mutating the process-global TZ (same approach as clock_paint.cpp).
struct SafeLocal {
  std::tm tm{};
  long utcOffset = 0;
};

SafeLocal safe_local(std::time_t now, const std::string& tzName) {
  SafeLocal out{};
  auto fallback = [&]() {
    if (localtime_r(&now, &out.tm) != nullptr) {
#if defined(__GLIBC__) || defined(__linux__)
      out.utcOffset = out.tm.tm_gmtoff;
#endif
    }
  };
  if (tzName.empty()) {
    fallback();
    return out;
  }
  const auto* tz = std::chrono::locate_zone(tzName);
  if (tz == nullptr) {
    fallback();
    return out;
  }
  const auto zt = std::chrono::zoned_time(tz, std::chrono::system_clock::from_time_t(now));
  const auto tp = std::chrono::floor<std::chrono::seconds>(zt.get_local_time());
  const auto ldp = std::chrono::floor<std::chrono::days>(tp);
  std::chrono::hh_mm_ss hms{tp - ldp};
  // Interpret the local day count as a civil (sys) day: its encoding is the
  // local civil date, which is what the tm fields need.
  const auto sdp = std::chrono::sys_days{ldp.time_since_epoch()};
  const auto ymd = std::chrono::year_month_day{sdp};
  out.tm.tm_year = static_cast<int>(ymd.year()) - 1900;
  out.tm.tm_mon = static_cast<int>(static_cast<unsigned>(ymd.month())) - 1;
  out.tm.tm_mday = static_cast<unsigned>(ymd.day());
  out.tm.tm_wday = static_cast<unsigned>(std::chrono::weekday{sdp}.c_encoding());
  out.tm.tm_yday = 0;
  out.tm.tm_hour = static_cast<int>(hms.hours().count());
  out.tm.tm_min = static_cast<int>(hms.minutes().count());
  out.tm.tm_sec = static_cast<int>(hms.seconds().count());
  out.tm.tm_isdst = 0;
  out.utcOffset =
      static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(zt.get_info().offset).count());
  return out;
}

std::string strftime_or_empty(const char* fmt, const std::tm& t) {
  char buf[256];
  if (std::strftime(buf, sizeof(buf), fmt, &t) == 0) return {};
  return buf;
}

}  // namespace

DashClockText dashboard_clock_text(const eh::config::ShellConfig& sc) {
  DashClockText out;
  const std::time_t now = std::time(nullptr);
  const SafeLocal lt = safe_local(now, sc.time.timezone);

  if (!sc.time.customFormat.empty()) {
    out.time = strftime_or_empty(sc.time.customFormat.c_str(), lt.tm);
    out.date.clear();
  } else {
    const char* timeFmt = sc.time.use24h ? (sc.time.showSeconds ? "%H:%M:%S" : "%H:%M")
                                         : (sc.time.showSeconds ? "%I:%M:%S" : "%I:%M");
    out.time = strftime_or_empty(timeFmt, lt.tm);
    if (sc.time.showDate) out.date = strftime_or_empty("%a, %b %d", lt.tm);
  }
  if (!sc.time.timezone.empty()) {
    out.zone = sc.time.timezone;
  } else {
    const char* z = std::getenv("TZ");
    out.zone = (z && *z) ? z : "local";
    const auto p = out.zone.find('/');
    if (p != std::string::npos) out.zone = out.zone.substr(p + 1);
    for (char& ch : out.zone)
      if (ch == '_') ch = ' ';
  }
  return out;
}

void dashboard_sample_system(DockApp& app) {
  auto& d = app.dash;
  const std::uint64_t now = eh::shell::now_mono_ms();
  if (d.sysPrimed && d.sysSampleMs != 0 && now - d.sysSampleMs < 1000) return;
  d.sysSampleMs = now;

  if (FILE* f = std::fopen("/proc/stat", "r")) {
    char line[512];
    if (std::fgets(line, sizeof(line), f)) {
      unsigned long long u = 0, n = 0, s = 0, i = 0, io = 0, irq = 0, sirq = 0, steal = 0;
      std::sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &i, &io, &irq, &sirq, &steal);
      const unsigned long long idle = i + io;
      const unsigned long long total = u + n + s + i + io + irq + sirq + steal;
      if (d.sysPrimed && total > d.sysPrevTotal) {
        const unsigned long long dIdle = idle - d.sysPrevIdle;
        const unsigned long long dTotal = total - d.sysPrevTotal;
        if (dTotal > 0) {
          const double pct = 100.0 * (1.0 - static_cast<double>(dIdle) / static_cast<double>(dTotal));
          d.sysCpuPct = std::clamp(pct, 0.0, 100.0);
        }
      }
      d.sysPrevIdle = idle;
      d.sysPrevTotal = total;
    }
    std::fclose(f);
  }

  if (FILE* f = std::fopen("/proc/meminfo", "r")) {
    double totalKb = 0, availKb = 0;
    char key[64];
    unsigned long long val = 0;
    char unit[32];
    while (std::fscanf(f, "%63s %llu %31s", key, &val, unit) == 3) {
      if (std::strncmp(key, "MemTotal:", 8) == 0) totalKb = static_cast<double>(val);
      else if (std::strncmp(key, "MemAvailable:", 13) == 0) availKb = static_cast<double>(val);
      if (totalKb > 0 && availKb > 0) break;
    }
    std::fclose(f);
    if (totalKb > 0) {
      d.sysMemTotalMb = totalKb / 1024.0;
      d.sysMemMb = (totalKb - availKb) / 1024.0;
    }
  }

  if (FILE* f = std::fopen("/sys/class/thermal/thermal_zone0/temp", "r")) {
    double milli = 0;
    if (std::fscanf(f, "%lf", &milli) == 1 && milli > 0.0) d.sysTempC = milli / 1000.0;
    std::fclose(f);
  }

  d.sysPrimed = true;
}

std::string dashboard_weather_instance_id(const DockApp& app) {
  if (!app.weatherInstanceId.empty()) return app.weatherInstanceId;
  auto scan = [](const std::vector<std::string>& v) -> std::string {
    for (const auto& id : v)
      if (eh::config::widget_implementation_type(id) == "weather") return id;
    return {};
  };
  std::string id = scan(app.settings.leftWidgets);
  if (id.empty()) id = scan(app.settings.centerWidgets);
  if (id.empty()) id = scan(app.settings.rightWidgets);
  return id.empty() ? std::string("control_center") : id;
}

}  // namespace eh::shell::dashboard
