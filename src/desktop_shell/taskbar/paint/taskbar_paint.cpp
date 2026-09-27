#include <dirent.h>

#include "desktop_shell/taskbar/paint/taskbar_paint.hpp"
#include "desktop_shell/taskbar/core/taskbar.hpp"
#include "desktop_shell/shared/core/running_snapshot.hpp"
#include "desktop_shell/shared/pins/pin_identity.hpp"

#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/shared/pins/pin_identity.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/asset/asset_loader.hpp"
#include "desktop_shell/common/os_logo/os_logo.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"
#include "desktop_shell/widgets/shared/shared_slot_paint.hpp"
#include "desktop_shell/widgets/shared/widget_settings.hpp"
#include "services/tray/filter/tray_env_filter.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/log/debug_log.hpp"

#include <cairo/cairo.h>

#include <cstdio>
#include <unordered_set>

#include <algorithm>
#include <array>
#include <chrono>

namespace eh::shell::taskbar {

cairo_surface_t* TaskbarScaledIconCache::getOrScale(cairo_surface_t* src, int tw, int th) {
  if (!src || tw <= 0 || th <= 0) return nullptr;
  if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) return nullptr;
  Key k{src, tw, th};
  auto it = map.find(k);
  if (it != map.end()) { hits++; return it->second; }
  misses++;

  const int sw = cairo_image_surface_get_width(src);
  const int sh = cairo_image_surface_get_height(src);
  if (sw <= 0 || sh <= 0) return nullptr;
  /* Already the right size: no extra surface, just use the source directly. */
  if (sw == tw && sh == th) return src;

  cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
  if (cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) {
    if (out) cairo_surface_destroy(out);
    return nullptr;
  }
  cairo_t* cr = cairo_create(out);
  cairo_scale(cr, static_cast<double>(tw) / sw, static_cast<double>(th) / sh);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_paint(cr);
  cairo_destroy(cr);
  if (cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(out);
    return nullptr;
  }
  if (map.size() >= kCap && !fifo.empty()) {
    auto oit = map.find(fifo.front());
    if (oit != map.end()) {
      if (oit->second) cairo_surface_destroy(oit->second);
      map.erase(oit);
    }
    fifo.erase(fifo.begin());
  }
  map.emplace(k, out);
  fifo.push_back(k);
  return out;
}

}
#include <cmath>
#include <cstdint>
#include <numbers>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct TbLrStripLayout {
  bool side_by_side = false;
  double x_left = 0;
  double x_right = 0;
};

TbLrStripLayout tb_lr_strip_layout(double pill_x, double box_w, double inner,
                                    double lw, double cw, double rw, double secGap) {
   
  TbLrStripLayout L{};
  if (cw > 1e-6) return L;
  const double half = inner * 0.5;
  const double minX = pill_x + half;
  const double maxR = pill_x + box_w - half;
  const double need = lw + secGap + rw;
  if (need > maxR - minX + 1e-9) return L;
  const double rStart = maxR - rw;
  const double idealL = rStart - lw - secGap;
  if (idealL < minX - 1e-9) return L;
  L.side_by_side = true;
  L.x_left = idealL;
  L.x_right = rStart;
  return L;
}

double tb_strip_h_scale(double box_w, double total_w, double inner) {
   
  const double avail = std::max(1.0, box_w - inner);
  if (total_w <= avail) return 1.0;
  return avail / total_w;
}

}

namespace {

constexpr double kRunIndicatorGapPx = 0.0;
constexpr double kPressIconScale = 0.92;
constexpr double kPressLiftPx = 2.0;

// kHiResIconPx: see icon_cache.hpp — avoids capping the raster below what a
// scaled/HiDPI taskbar actually needs on screen (fixes upscaled/blurry icons).
const eh::icons::IconEntry* paint_app_icon(eh::icons::IconCache& icons, const std::string& key) {
  return icons.app_icon(key, eh::icons::kHiResIconPx);
}

const eh::icons::IconEntry* paint_tray_icon(eh::icons::IconCache& icons, const std::string& name) {
  return icons.tray_icon(name);
}

[[nodiscard]] const eh::shell::taskbar::TaskbarTrayItem* find_tray_item(
    const std::vector<eh::shell::taskbar::TaskbarTrayItem>& traySnap,
    const std::string& key) {
   
  if (key.rfind(eh::shell::kSlotKeyTray, 0) != 0) return nullptr;
  const std::string want = key.substr(std::string(eh::shell::kSlotKeyTray).size());
  for (const auto& cand : traySnap) {
    if ((cand.service + cand.path) == want) return &cand;
  }
  return nullptr;
}

[[nodiscard, maybe_unused]] const eh::wayland::ForeignToplevels::Toplevel* toplevel_by_serial(
    const eh::wayland::ForeignToplevels& tl, std::uint64_t serial) {
   
  if (serial == 0) return nullptr;
  for (const auto& t : tl) {
    if (t.serial == serial && t.handle && !t.closed) return &t;
  }
  return nullptr;
}

// Win7-style icon+text labels for running app slots. Returns the label text
// (window title of the slot's active window) or empty when the slot should
// stay icon-only (labels off, or nothing running).
inline std::string tb_app_slot_label(eh::shell::taskbar::TaskbarApp& app, std::uint64_t chosenSerial) {
  if (!app.effShowLabels) return {};
  if (chosenSerial == 0 || !app.toplevels) return {};
  const auto* tl = toplevel_by_serial(*app.toplevels, chosenSerial);
  if (!tl || tl->title.empty()) return {};
  return tl->title;
}

inline double tb_label_font_px(double uiScale) { return 12.0 * uiScale; }

// Shrink-to-fit budget for this frame (0 = full default width).
inline double tb_label_max_w(const eh::shell::taskbar::TaskbarApp& app, double uiScale) {
  if (app.effLabelMaxW > 0.0) return app.effLabelMaxW;
  return 140.0 * uiScale;
}
inline double tb_label_min_w(double uiScale) { return 40.0 * uiScale; }

inline cairo_t* tb_label_measure_cr(eh::shell::taskbar::TaskbarApp& app, double fontPx) {
  if (!app.labelMeasureCr) {
    app.labelMeasureSurf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    if (!app.labelMeasureSurf) return nullptr;
    app.labelMeasureCr = cairo_create(app.labelMeasureSurf);
    if (!app.labelMeasureCr) {
      cairo_surface_destroy(app.labelMeasureSurf);
      app.labelMeasureSurf = nullptr;
      return nullptr;
    }
  }
  cairo_select_font_face(app.labelMeasureCr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(app.labelMeasureCr, fontPx);
  return app.labelMeasureCr;
}

inline double tb_label_text_width(eh::shell::taskbar::TaskbarApp& app, const std::string& text, double fontPx) {
  if (text.empty() || fontPx <= 0.0) return 0.0;
  std::string key = text;
  key.push_back('|');
  key += std::to_string(fontPx);
  auto it = app.labelWidthCache.find(key);
  if (it != app.labelWidthCache.end()) return it->second;
  double w = 0.0;
  if (cairo_t* mcr = tb_label_measure_cr(app, fontPx)) {
    cairo_text_extents_t ex{};
    cairo_text_extents(mcr, text.c_str(), &ex);
    w = std::max(0.0, ex.x_advance);
  }
  if (app.labelWidthCache.size() > 1024) app.labelWidthCache.clear();
  app.labelWidthCache.emplace(std::move(key), w);
  return w;
}

// Trim text with an ellipsis so its advance fits maxW. Returns the trimmed
// string (may equal the input).
inline std::string tb_label_ellipsize(eh::shell::taskbar::TaskbarApp& app, const std::string& text,
                                      double fontPx, double maxW) {
  if (tb_label_text_width(app, text, fontPx) <= maxW) return text;
  static constexpr const char* kEll = "\xe2\x80\xa6";
  const double ellW = tb_label_text_width(app, kEll, fontPx);
  std::string out;
  // Walk UTF-8 code points so multibyte titles never split mid-sequence.
  size_t i = 0;
  while (i < text.size()) {
    size_t len = 1;
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if ((c & 0x80) == 0) len = 1;
    else if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    out.append(text, i, len);
    i += len;
    if (tb_label_text_width(app, out, fontPx) + ellW > maxW) {
      // Drop the point that overflowed, then back off one more so the
      // ellipsis itself always fits.
      out.erase(out.size() - len);
      while (!out.empty() && tb_label_text_width(app, out, fontPx) + ellW > maxW) {
        // Remove one trailing UTF-8 point.
        size_t e = out.size();
        do { --e; } while (e > 0 && (static_cast<unsigned char>(out[e]) & 0xC0) == 0x80);
        out.erase(e);
      }
      break;
    }
  }
  out += kEll;
  return out;
}

inline bool win7_token_match(const std::string& wid) {
  return wid == "win7_tasks" || eh::config::widget_implementation_type(wid) == "win7_tasks";
}

// A placed + enabled Win7 tasklist supersedes the classic pinned/running
// widgets bar-wide: otherwise every window renders twice (once per strip)
// and the bar overflows into squashed, overlapping buttons.
inline bool win7_widget_active(const eh::config::ShellConfig& sc) {
  const std::vector<const std::vector<std::string>*> sections{
      &sc.taskbar.leftWidgets, &sc.taskbar.centerWidgets, &sc.taskbar.rightWidgets};
  for (const auto* sec : sections) {
    for (const auto& wid : *sec) {
      if (!win7_token_match(wid)) continue;
      if (eh::config::widget_instance_enabled(sc, wid)) return true;
    }
  }
  return false;
}

// Unified Win7 buttons: pins in settings order (with running state), then
// running groups not covered by a pin — one slot per app, launch-or-switch.
struct Win7SlotDesc {
  std::string key;
  std::string iconId;
  std::uint64_t serial = 0;
  bool active = false;
  bool pinned = false;
};

inline std::vector<Win7SlotDesc> win7_slot_descs(eh::shell::taskbar::TaskbarApp& app,
                                                 const eh::shell::shared::RunningSnapshot& snap) {
  std::vector<Win7SlotDesc> out;
  auto covered_by_pin = [&](const std::string& k) -> bool {
    for (const auto& pRaw : app.settings.pinnedApps) {
      const std::string pn = eh::shell::paths::normalize_desktop_app_id(pRaw);
      if (pn == "unknown" || pn == eh::shell::kSettingsAppId) continue;
      if (pn == k || pin_identity_same_resolved_desktop(pn, k)) return true;
    }
    return false;
  };
  for (const auto& pRaw : app.settings.pinnedApps) {
    const std::string p = eh::shell::paths::normalize_desktop_app_id(pRaw);
    if (p == "unknown" || p == eh::shell::kSettingsAppId) continue;
    Win7SlotDesc d;
    d.key = p;
    d.iconId = pRaw;
    d.pinned = true;
    for (const auto& rg : snap.groups) {
      if (pin_identity_pin_raw_matches_key(pRaw, rg.pinMatchKey)) {
        d.serial = rg.chosenSerial;
        d.active = rg.anyActivated;
        if (!rg.iconId.empty()) d.iconId = rg.iconId;
        break;
      }
    }
    out.push_back(std::move(d));
  }
  for (const auto& rg : snap.groups) {
    if (covered_by_pin(rg.pinMatchKey)) continue;
    Win7SlotDesc d;
    d.key = rg.key;
    d.iconId = rg.iconId;
    d.serial = rg.chosenSerial;
    d.active = rg.anyActivated;
    out.push_back(std::move(d));
  }
  return out;
}

// Color Hot-track (Win7): dominant icon color via a 27-bucket RGB
// histogram (channel thirds 0-60/60-200/200-255), transparent pixels
// skipped, black/white/gray buckets disqualified, first-wins ties.
inline bool tb_hot_track_color(eh::shell::taskbar::TaskbarApp& app, const std::string& iconKey,
                               double* r, double* g, double* b) {
  auto it = app.hotTrackCache.find(iconKey);
  if (it != app.hotTrackCache.end()) {
    *r = it->second[0];
    *g = it->second[1];
    *b = it->second[2];
    return it->second[3] > 0.5f;
  }
  bool ok = false;
  float cr = 0.55f, cg = 0.75f, cb = 1.0f;
  const auto tCompute0 = std::chrono::steady_clock::now();
  if (const auto* ic = paint_app_icon(app.icons, iconKey)) {
    cairo_surface_t* surf = ic->surface;
    if (surf && cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS &&
        cairo_surface_get_type(surf) == CAIRO_SURFACE_TYPE_IMAGE) {
      cairo_surface_flush(surf);
      const int w = cairo_image_surface_get_width(surf);
      const int h = cairo_image_surface_get_height(surf);
      const int stride = cairo_image_surface_get_stride(surf);
      const unsigned char* data = cairo_image_surface_get_data(surf);
      if (data && w > 0 && h > 0) {
        // BGRA bytes on little-endian (matches ARGB32 icon rasters).
        long buckets[3][3][3] = {};
        const int step = std::max(1, std::min(w, h) / 48);
        auto third = [](int v) { return (v <= 60) ? 0 : ((v <= 200) ? 1 : 2); };
        for (int y = 0; y < h; y += step) {
          const unsigned char* row = data + static_cast<ptrdiff_t>(y) * stride;
          for (int x = 0; x < w; x += step) {
            const unsigned char* px = row + x * 4;
            if (px[3] < 128) continue;
            const int bi = third(px[2]);
            const int gi = third(px[1]);
            const int ri = third(px[0]);
            if (bi == gi && gi == ri) continue;
            ++buckets[bi][gi][ri];
          }
        }
        long best = 0;
        int bb = -1, bg = -1, br = -1;
        for (int bi = 0; bi < 3; ++bi)
          for (int gi = 0; gi < 3; ++gi)
            for (int ri = 0; ri < 3; ++ri) {
              if (buckets[bi][gi][ri] > best) {
                best = buckets[bi][gi][ri];
                bb = bi;
                bg = gi;
                br = ri;
              }
            }
        static constexpr float kCent[3] = {30.0f / 255.0f, 130.0f / 255.0f, 228.0f / 255.0f};
        if (best > 0 && bb >= 0) {
          cr = kCent[br];
          cg = kCent[bg];
          cb = kCent[bb];
          ok = true;
        }
      }
    }
  }
  if (app.hotTrackCache.size() > 256) app.hotTrackCache.clear();
  app.hotTrackCache.emplace(iconKey, std::array<float, 4>{cr, cg, cb, ok ? 1.0f : 0.0f});
  const double computeMs =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tCompute0).count();
  ++app.hotTrackComputes;
  app.hotTrackComputeMs += computeMs;
  if (computeMs > 4.0) {
    debug_log("taskbar-perf", "slow hot-track compute=%.1fms icon=%s", computeMs, iconKey.c_str());
  }
  *r = cr;
  *g = cg;
  *b = cb;
  return ok;
}

// Windows belonging to one bar group (for the overflow chevron): an
// ungrouped key resolves to its single window, a grouped key to every live
// window of the app.
inline std::vector<std::pair<uint64_t, std::string>> tb_group_windows(
    eh::shell::taskbar::TaskbarApp& app, const std::string& groupKey,
    std::uint64_t chosenSerial) {
  std::vector<std::pair<uint64_t, std::string>> out;
  if (!app.toplevels) return out;
  if (groupKey.rfind("__eh_tl_", 0) == 0) {
    if (const auto* tl = toplevel_by_serial(*app.toplevels, chosenSerial)) {
      if (tl->handle && !tl->closed) out.emplace_back(tl->serial, tl->title);
    }
    return out;
  }
  for (const auto& tl : *app.toplevels) {
    if (!tl.handle || tl.closed) continue;
    if (eh::shell::paths::normalize_desktop_app_id(tl.appId) == groupKey)
      out.emplace_back(tl.serial, tl.title);
  }
  std::sort(out.begin(), out.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  return out;
}

// Live window count for a slot's group: 0 when nothing running, 1 for a
// single window, N for a stacked group (Win7's layered-edges indicator).
inline int tb_group_window_count(eh::shell::taskbar::TaskbarApp& app, const std::string& groupKey,
                                 std::uint64_t chosenSerial) {
  if (chosenSerial == 0 || !app.toplevels) return 0;
  if (groupKey.rfind("__eh_tl_", 0) == 0) {
    const auto* tl = toplevel_by_serial(*app.toplevels, chosenSerial);
    return (tl && tl->handle && !tl->closed) ? 1 : 0;
  }
  int n = 0;
  for (const auto& tl : *app.toplevels) {
    if (!tl.handle || tl.closed) continue;
    if (eh::shell::paths::normalize_desktop_app_id(tl.appId) == groupKey) ++n;
  }
  return n;
}

// Extra slot width for the label: icon + gap + capped text + trailing pad.
// Returns 0 when the slot stays icon-only.
inline double tb_app_label_extra_w(eh::shell::taskbar::TaskbarApp& app, std::uint64_t chosenSerial,
                                   double uiScale) {
  const std::string label = tb_app_slot_label(app, chosenSerial);
  if (label.empty()) return 0.0;
  const double fontPx = tb_label_font_px(uiScale);
  const double textW = std::min(tb_label_text_width(app, label, fontPx), tb_label_max_w(app, uiScale));
  return 6.0 * uiScale + textW + 8.0 * uiScale;
}

}

namespace eh::shell::taskbar {

TaskbarSectionWidths taskbar_measure_sections(TaskbarApp& app,
                                              const std::vector<std::string>& leftW,
                                              const std::vector<std::string>& centerW,
                                              const std::vector<std::string>& rightW,
                                              const eh::shell::shared::RunningSnapshot& runningSnap,
                                              const eh::mpris::PlayerSnapshot& mprisSnap) {
   
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const double globalScale = std::clamp(sc.dock.shellUiScale, 0.5, 2.0);
  const double taskbarUIScale = std::clamp(app.settings.scale, 0.5, 2.0) * globalScale;
  const double barH = static_cast<double>(app.settings.height);
  const double iconRaw = static_cast<double>(app.settings.iconSize) * taskbarUIScale;
  const double icon = std::clamp(iconRaw, 8.0, std::max(8.0, barH - 8.0));
  const double gap = static_cast<double>(app.settings.iconSpacing) * taskbarUIScale;

  struct Slot {
    using Kind = eh::widgets::shared_slot_paint::SlotKind;
    Kind kind = Kind::App;
    bool isPinned = false;
    std::string key;
    std::uint64_t chosenSerial = 0;
    bool win7 = false;
  };
  std::vector<Slot> pinnedSlots, runningSlots, traySlots;
  pinnedSlots.reserve(app.settings.pinnedApps.size());
  runningSlots.reserve(runningSnap.groups.size());
  {
    std::lock_guard<std::mutex> lock(app.trayMutex);
    traySlots.resize(app.trayItems.size());
  }

  auto is_pinned_key = [&](const std::string& k) -> bool {
    for (const auto& pRaw : app.settings.pinnedApps) {
      const std::string pn = eh::shell::paths::normalize_desktop_app_id(pRaw);
      if (pn == "unknown" || pn == eh::shell::kSettingsAppId) continue;
      if (pn == k || pin_identity_same_resolved_desktop(pn, k)) return true;
    }
    return false;
  };
  for (const auto& pRaw : app.settings.pinnedApps) {
    const std::string p = eh::shell::paths::normalize_desktop_app_id(pRaw);
    if (p == "unknown" || p == eh::shell::kSettingsAppId) continue;
    Slot s;
    s.isPinned = true;
    s.key = p;
    for (const auto& rg : runningSnap.groups) {
      if (pin_identity_pin_raw_matches_key(pRaw, rg.pinMatchKey)) {
        s.chosenSerial = rg.chosenSerial;
        break;
      }
    }
    pinnedSlots.push_back(std::move(s));
  }
  for (const auto& rg : runningSnap.groups) {
    if (is_pinned_key(rg.pinMatchKey)) continue;
    Slot s;
    s.key = rg.key;
    s.chosenSerial = rg.chosenSerial;
    runningSlots.push_back(std::move(s));
  }

  Slot settingsSlot, spotlightSlot, appMenuSlot, appDrawerSlot;
  settingsSlot.kind = Slot::Kind::Settings;
  spotlightSlot.kind = Slot::Kind::Spotlight;
  appMenuSlot.kind = Slot::Kind::AppMenu;
  appDrawerSlot.kind = Slot::Kind::AppDrawer;
  auto widget_blocked = [&](const std::string& w) -> bool {
    if (app.settings.widgetsEnabled) return false;
    if (w == "pinned_apps" || w == "running_apps" || w == "win7_tasks") return false;
    if (eh::config::widget_token_is_system_tray(w)) return false;
    if (w == "settings_button" || w == "distro_spotlight" || w == "app_menu" ||
        w == "trash") return false;
    const std::string impl = eh::config::widget_implementation_type(w);
    return impl == "clock" || impl == "world_clock" || impl == "weather" || impl == "media" || impl == "workspaces" ||
           impl == "control_center" || impl == "notifications" || impl == "volume_mixer" ||
           impl == "app_drawer";
  };

  auto slot_width = [&](const Slot& s) -> double {
    if (s.kind == Slot::Kind::Clock)
      return eh::shell::dock_slot_hooks::dock_clock_slot_width(nullptr, sc, s.key, icon, barH);
    if (s.kind == Slot::Kind::Weather)
      return eh::shell::dock_slot_hooks::dock_weather_slot_width(nullptr, sc, s.key, icon, barH);
    if (s.kind == Slot::Kind::Separator)
      return std::max(4.0, 6.0 * taskbarUIScale);
    if (s.kind == Slot::Kind::Media) {
      return eh::shell::dock_slot_hooks::dock_media_slot_width(nullptr, sc, s.key, icon, barH, mprisSnap,
                                                               app.settings.compactMedia);
    }
    if (s.kind == Slot::Kind::Workspaces)
      return eh::shell::dock_slot_hooks::dock_workspaces_slot_width(nullptr, sc, s.key, icon, barH, app.workspaceStrip);
    if (s.kind == Slot::Kind::ControlCenter)
      return eh::shell::dock_slot_hooks::dock_control_center_slot_width(sc, s.key, icon, barH);
    if (s.kind == Slot::Kind::Battery)
      return eh::shell::dock_slot_hooks::dock_battery_slot_width(nullptr, sc, s.key, icon, barH);
    if (s.kind == Slot::Kind::Bluetooth)
      return eh::shell::dock_slot_hooks::dock_bluetooth_slot_width(nullptr, sc, s.key, icon, barH);
    if (s.kind == Slot::Kind::WorldClock)
      return eh::shell::dock_slot_hooks::dock_world_clock_slot_width(nullptr, sc, s.key, icon, barH);
    if (s.kind == Slot::Kind::App)
      return icon + tb_app_label_extra_w(app, s.chosenSerial, taskbarUIScale);
    return icon;
  };

  auto append_widget = [&](std::vector<Slot>& out, const std::string& wid) {
    if (widget_blocked(wid)) return;
    if (wid == "pinned_apps") {
      if (!win7_widget_active(sc)) {
        for (const auto& s : pinnedSlots) out.push_back(s);
      }
    }
    else if (wid == "running_apps") {
      if (!win7_widget_active(sc)) {
        for (const auto& s : runningSlots) out.push_back(s);
      }
    }
    else if (win7_token_match(wid)) {
      for (auto& d : win7_slot_descs(app, runningSnap)) {
        Slot s;
        s.key = std::move(d.key);
        s.chosenSerial = d.serial;
        s.isPinned = d.pinned;
        s.win7 = true;
        out.push_back(std::move(s));
      }
    }
    else if (eh::config::widget_token_is_system_tray(wid)) { for (const auto& s : traySlots) out.push_back(s); }
    else if (wid == "settings_button") out.push_back(settingsSlot);
    else if (wid == "distro_spotlight") out.push_back(spotlightSlot);
    else if (wid == "app_menu") out.push_back(appMenuSlot);
    else if (eh::config::widget_implementation_type(wid) == "app_drawer") out.push_back(appDrawerSlot);
    else if (eh::config::widget_implementation_type(wid) == "smenu") { Slot sm; sm.kind = Slot::Kind::Smenu; sm.key = wid; out.push_back(std::move(sm)); }
    else if (eh::config::widget_implementation_type(wid) == "clock") { Slot cs; cs.kind = Slot::Kind::Clock; cs.key = wid; out.push_back(std::move(cs)); }
    else if (eh::config::widget_implementation_type(wid) == "weather") { Slot ws; ws.kind = Slot::Kind::Weather; ws.key = wid; out.push_back(std::move(ws)); }
    else if (eh::config::widget_implementation_type(wid) == "media") { Slot ms; ms.kind = Slot::Kind::Media; ms.key = wid; out.push_back(std::move(ms)); }
    else if (eh::config::widget_implementation_type(wid) == "workspaces") { Slot ws; ws.kind = Slot::Kind::Workspaces; ws.key = wid; out.push_back(std::move(ws)); }
    else if (eh::config::widget_implementation_type(wid) == "control_center") { Slot cc; cc.kind = Slot::Kind::ControlCenter; cc.key = wid; out.push_back(std::move(cc)); }
    else if (wid == "trash" || eh::config::widget_implementation_type(wid) == "trash") { Slot ts; ts.kind = Slot::Kind::Trash; ts.key = wid; out.push_back(std::move(ts)); }
    else if (eh::config::widget_implementation_type(wid) == "volume_mixer") { Slot vm; vm.kind = Slot::Kind::VolumeMixer; vm.key = wid; out.push_back(std::move(vm)); }
    else if (eh::config::widget_implementation_type(wid) == "battery") { Slot bs; bs.kind = Slot::Kind::Battery; bs.key = wid; out.push_back(std::move(bs)); }
    else if (eh::config::widget_implementation_type(wid) == "bluetooth") { Slot bts; bts.kind = Slot::Kind::Bluetooth; bts.key = wid; out.push_back(std::move(bts)); }
    else if (eh::config::widget_implementation_type(wid) == "vpn") { Slot vs; vs.kind = Slot::Kind::Vpn; vs.key = wid; out.push_back(std::move(vs)); }
    else if (eh::config::widget_implementation_type(wid) == "world_clock") { Slot wcs; wcs.kind = Slot::Kind::WorldClock; wcs.key = wid; out.push_back(std::move(wcs)); }
  };

  auto build_section = [&](const std::vector<std::string>& widgets) {
    std::vector<Slot> out;
    for (const auto& w : widgets) append_widget(out, w);
    return out;
  };

  std::vector<Slot> left = build_section(leftW);
  std::vector<Slot> center = build_section(centerW);
  std::vector<Slot> right = build_section(rightW);

  std::vector<Slot> all;
  all.reserve(left.size() + center.size() + right.size());
  all.insert(all.end(), left.begin(), left.end());
  all.insert(all.end(), center.begin(), center.end());
  all.insert(all.end(), right.begin(), right.end());

  // Gap rules shared with the paint pass (taskbar_paint_widget_bar): tray
  // items pack together, and app groups pack only when their tray pill is on.
  auto gap_between = [&](const Slot& a, const Slot& b) -> double {
    if (a.kind == Slot::Kind::Tray && b.kind == Slot::Kind::Tray) return 0.0;
    if (a.kind == Slot::Kind::App && a.isPinned && b.kind == Slot::Kind::App && b.isPinned)
      return app.settings.pinnedAppsTrayPill ? 0.0 : gap;
    if (a.kind == Slot::Kind::App && !a.isPinned && b.kind == Slot::Kind::App && !b.isPinned)
      return app.settings.runningAppsTrayPill ? 0.0 : gap;
    return gap;
  };

  auto section_w = [&](const std::vector<Slot>& slots) -> double {
    double tw = 0.0;
    for (size_t i = 0; i < slots.size(); i++) {
      tw += slot_width(slots[i]);
      if (i + 1 < slots.size()) tw += gap_between(slots[i], slots[i + 1]);
    }
    return tw;
  };

  TaskbarSectionWidths out;
  out.left = section_w(left);
  out.center = section_w(center);
  out.right = section_w(right);
  double totalW = 0.0;
  for (size_t i = 0; i < all.size(); i++) {
    totalW += slot_width(all[i]);
    if (i + 1 < all.size()) totalW += gap_between(all[i], all[i + 1]);
  }
  out.total = totalW;
  return out;
}

void taskbar_paint_widget_bar(TaskbarApp& app, cairo_t* cr,
                               double x, double y, double boxW, double boxH,
                               const std::vector<std::string>& leftW,
                               const std::vector<std::string>& centerW,
                               const std::vector<std::string>& rightW,
                               std::vector<TaskbarWidgetHit>* out_hits,
                               int taskbarHoverSlot, int taskbarPressedSlot,
                               double taskbarHoverLiftPx,
                               bool use_panel,
                               const eh::shell::shared::RunningSnapshot& runningSnap,
                               const eh::mpris::PlayerSnapshot& mprisSnap,
                               int layerIdx) {
  const auto tSetup0 = std::chrono::steady_clock::now();
  const auto tA0 = std::chrono::steady_clock::now();
  eh::widgets::slot_pill_style::g_opacityScale = static_cast<double>(std::clamp(app.settings.slotPillOpacity, 0, 100)) / 100.0;
  const eh::config::ShellAppearance ap = eh::config::shell_config_snapshot().appearance;
  const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(ap);
  {
    const double baseR = mc.dockFillR * 0.6 + mc.accentR * 0.15;
    const double baseG = mc.dockFillG * 0.6 + mc.accentG * 0.15;
    const double baseB = mc.dockFillB * 0.6 + mc.accentB * 0.15;
    eh::widgets::slot_pill_style::g_pillR = baseR;
    eh::widgets::slot_pill_style::g_pillG = baseG;
    eh::widgets::slot_pill_style::g_pillB = baseB;
  }
  eh::widgets::slot_pill_style::g_hoverAccentR = mc.accentR;
  eh::widgets::slot_pill_style::g_hoverAccentG = mc.accentG;
  eh::widgets::slot_pill_style::g_hoverAccentB = mc.accentB;
  eh::widgets::slot_pill_style::g_mediaBtnR = mc.dockFillR * 0.6 + mc.accentR * 0.15;
  eh::widgets::slot_pill_style::g_mediaBtnG = mc.dockFillG * 0.6 + mc.accentG * 0.15;
  eh::widgets::slot_pill_style::g_mediaBtnB = mc.dockFillB * 0.6 + mc.accentB * 0.15;
  eh::widgets::slot_pill_style::g_mediaGlyphR = mc.textR;
  eh::widgets::slot_pill_style::g_mediaGlyphG = mc.textG;
  eh::widgets::slot_pill_style::g_mediaGlyphB = mc.textB;
  eh::widgets::slot_pill_style::g_mediaOnAccentR = mc.dockFillR;
  eh::widgets::slot_pill_style::g_mediaOnAccentG = mc.dockFillG;
  eh::widgets::slot_pill_style::g_mediaOnAccentB = mc.dockFillB;
  const bool matugen = ap.anyPaletteActive();

  eh::shell::dock_slot_hooks::workspace_strip_poll(app.workspaceStrip, app.workspaceStripLastPoll, app.compositorKind);

  auto rounded_rect = [&](double rx, double ry, double rw, double rh, double r) {
    const double x0 = rx, y0 = ry, x1 = rx + rw, y1 = ry + rh;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x1 - r, y0 + r, r, -M_PI_2, 0);
    cairo_arc(cr, x1 - r, y1 - r, r, 0, M_PI_2);
    cairo_arc(cr, x0 + r, y1 - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
  };

  using Slot = eh::shell::taskbar::TaskbarPaintSlot;

  // Running groups come from the per-draw snapshot built in taskbar_draw().
  const std::vector<eh::shell::shared::RunningGroup>& runningGroups = runningSnap.groups;
  const uint64_t settingsChosenSerial = runningSnap.settingsChosenSerial;
  const bool settingsActivated = runningSnap.settingsActivated;
  app.sectionMs[5] += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - tA0).count();

  // Build tray snapshot
  const auto tB0 = std::chrono::steady_clock::now();
  std::vector<TaskbarTrayItem> traySnap;
  {
    std::lock_guard<std::mutex> lock(app.trayMutex);
    traySnap = app.trayItems;
  }
  std::sort(traySnap.begin(), traySnap.end(),
            [](const TaskbarTrayItem& a, const TaskbarTrayItem& b) {
              if (a.service != b.service) return a.service < b.service;
              return a.path < b.path;
            });
  traySnap.erase(std::remove_if(traySnap.begin(), traySnap.end(),
      [](const TaskbarTrayItem& t) {
        return eh::tray::tray_item_hidden_by_env(t.id, t.title, t.service, t.path);
      }), traySnap.end());

  // Refresh pin identity cache
  {
    std::vector<std::string> sortedP = app.settings.pinnedApps;
    std::sort(sortedP.begin(), sortedP.end());
    std::string fp;
    fp.reserve(sortedP.size() * 12u + 4u);
    for (const auto& p : sortedP) { fp.push_back('\0'); fp += p; }
    if (fp != app.pinIdentityFingerprint) {
      app.pinIdentityFingerprint = std::move(fp);
      app.pinIdentityKeys.clear();
      for (const auto& pRaw : app.settings.pinnedApps) {
        const std::string pn = eh::shell::paths::normalize_desktop_app_id(pRaw);
        if (pn == "unknown" || pn == eh::shell::kSettingsAppId) continue;
        app.pinIdentityKeys[pn] = pin_identity_keys_for_raw(pRaw);
      }
    }
  }

  auto is_pinned_pred = [&](const std::string& k) -> bool {
    for (const auto& pRaw : app.settings.pinnedApps) {
      const std::string pn = eh::shell::paths::normalize_desktop_app_id(pRaw);
      if (pn == "unknown" || pn == eh::shell::kSettingsAppId) continue;
      if (pn == k || pin_identity_same_resolved_desktop(pn, k)) return true;
      auto it = app.pinIdentityKeys.find(pn);
      if (it != app.pinIdentityKeys.end())
        for (const auto& ik : it->second)
          if (ik == k) return true;
    }
    return false;
  };

  app.sectionMs[6] += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - tB0).count();

  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();

  const auto tC0 = std::chrono::steady_clock::now();
  bool slotsMatch = app.slotsValid
      && app.slotFpLeftW == leftW
      && app.slotFpCenterW == centerW
      && app.slotFpRightW == rightW
      && app.slotFpPinned == app.settings.pinnedApps
      && app.slotFpPinDragging == app.pinDragging
      && app.slotFpPinDragKey == app.pinDragKey
      && app.slotFpConfigGen == eh::config::shell_config_generation();
  if (slotsMatch) {
    if (runningGroups.size() != app.slotFpRunningKeys.size()
        || traySnap.size() != app.slotFpTrayIds.size()) {
      slotsMatch = false;
    } else {
      for (size_t i = 0; i < runningGroups.size() && slotsMatch; ++i) {
        slotsMatch = runningGroups[i].key == app.slotFpRunningKeys[i]
            && runningGroups[i].chosenSerial == app.slotFpRunningSerials[i]
            && runningGroups[i].anyActivated == (bool)app.slotFpRunningActive[i];
      }
      for (size_t i = 0; i < traySnap.size() && slotsMatch; ++i) {
        slotsMatch = (traySnap[i].service + traySnap[i].path) == app.slotFpTrayIds[i];
      }
    }
  }
  if (!slotsMatch) {
  std::vector<Slot> pinnedSlots;
  std::vector<Slot> runningSlots;
  std::vector<Slot> traySlots;
  pinnedSlots.reserve(app.settings.pinnedApps.size());
  runningSlots.reserve(runningGroups.size());
  traySlots.reserve(traySnap.size());

  Slot settingsSlot;
  settingsSlot.kind = Slot::Kind::Settings;
  settingsSlot.key = kSlotKeySettings;
  settingsSlot.chosenSerial = settingsChosenSerial;
  settingsSlot.anyActivated = settingsActivated;

  Slot spotlightSlot;
  spotlightSlot.kind = Slot::Kind::Spotlight;
  spotlightSlot.key = kSlotKeySpotlight;

  Slot appMenuSlot;
  appMenuSlot.kind = Slot::Kind::AppMenu;
  appMenuSlot.key = kSlotKeyAppMenu;

  Slot appDrawerSlot;
  appDrawerSlot.kind = Slot::Kind::AppDrawer;
  appDrawerSlot.key = kSlotKeyAppDrawer;

  // Floating pin tracking for drag-to-reorder
  std::string floatingPinLookup{};
  bool floatingPinRunning = false;
  bool floatingPinActivated = false;

  const auto& pinSrc = (app.pinDragging && !app.pinDragPinsSnapshot.empty())
      ? (!app.pinDragPaintOrder.empty() ? app.pinDragPaintOrder : app.pinDragPinsSnapshot)
      : app.settings.pinnedApps;
  for (const auto& pRaw : pinSrc) {
    const std::string p = eh::shell::paths::normalize_desktop_app_id(pRaw);
    if (p == "unknown" || p == eh::shell::kSettingsAppId) continue;
    // Skip dragged pin slot during drag
    if (app.pinDragging && !app.pinDragKey.empty() && p == app.pinDragKey) {
      floatingPinLookup = pRaw;
      for (const auto& rg : runningGroups) {
        if (pin_identity_pin_raw_matches_key(pRaw, rg.pinMatchKey)) {
          floatingPinRunning = true;
          floatingPinActivated = rg.anyActivated;
          if (!rg.iconId.empty()) floatingPinLookup = rg.iconId;
          break;
        }
      }
      continue;
    }
    Slot s;
    s.kind = Slot::Kind::App;
    s.key = p;
    s.iconId = pRaw;
    s.isPinned = true;
    for (const auto& rg : runningGroups) {
      if (pin_identity_pin_raw_matches_key(pRaw, rg.pinMatchKey)) {
        s.chosenSerial = rg.chosenSerial;
        s.anyActivated = rg.anyActivated;
        if (!rg.iconId.empty()) s.iconId = rg.iconId;
        break;
      }
    }
    pinnedSlots.push_back(std::move(s));
  }

  for (const auto& rg : runningGroups) {
    if (is_pinned_pred(rg.pinMatchKey)) continue;
    Slot s;
    s.kind = Slot::Kind::App;
    s.key = rg.key;
    s.iconId = rg.iconId;
    s.chosenSerial = rg.chosenSerial;
    s.anyActivated = rg.anyActivated;
    s.isPinned = false;
    runningSlots.push_back(std::move(s));
  }

  for (const auto& ti : traySnap) {
    Slot s;
    s.kind = Slot::Kind::Tray;
    s.key.clear();
    s.key.reserve(8 + ti.service.size() + ti.path.size());
    s.key += kSlotKeyTray;
    s.key += ti.service;
    s.key += ti.path;
    traySlots.push_back(std::move(s));
  }

  auto widget_blocked = [&](const std::string& w) -> bool {
    if (app.settings.widgetsEnabled) return false;
    if (w == "pinned_apps" || w == "running_apps" || w == "win7_tasks") return false;
    if (eh::config::widget_token_is_system_tray(w)) return false;
    if (w == "settings_button" || w == "distro_spotlight" || w == "app_menu" ||
        w == "trash") return false;
    const std::string impl = eh::config::widget_implementation_type(w);
    return impl == "clock" || impl == "world_clock" || impl == "weather" || impl == "media" || impl == "workspaces" ||
           impl == "control_center" || impl == "notifications" || impl == "volume_mixer" ||
           impl == "app_drawer";
  };

  auto append_widget = [&](std::vector<Slot>& out, const std::string& wid) {
    if (widget_blocked(wid)) return;
    if (wid == "pinned_apps") {
      if (!win7_widget_active(sc)) {
        for (const auto& s : pinnedSlots) out.push_back(s);
      }
    } else if (wid == "running_apps") {
      if (!win7_widget_active(sc)) {
        for (const auto& s : runningSlots) out.push_back(s);
      }
    } else if (win7_token_match(wid)) {
      for (auto& d : win7_slot_descs(app, runningSnap)) {
        Slot s;
        s.kind = Slot::Kind::App;
        s.key = std::move(d.key);
        s.iconId = std::move(d.iconId);
        s.chosenSerial = d.serial;
        s.anyActivated = d.active;
        s.isPinned = d.pinned;
        s.win7 = true;
        out.push_back(std::move(s));
      }
    } else if (eh::config::widget_token_is_system_tray(wid)) {
      for (const auto& s : traySlots) out.push_back(s);
    } else if (wid == "settings_button") {
      out.push_back(settingsSlot);
    } else if (wid == "distro_spotlight") {
      out.push_back(spotlightSlot);
    } else if (wid == "app_menu") {
      out.push_back(appMenuSlot);
    } else if (eh::config::widget_implementation_type(wid) == "app_drawer") {
      out.push_back(appDrawerSlot);
    } else if (eh::config::widget_implementation_type(wid) == "smenu") {
      Slot sm; sm.kind = Slot::Kind::Smenu; sm.key = wid; out.push_back(std::move(sm));
    } else if (eh::config::widget_implementation_type(wid) == "clock") {
      Slot cs; cs.kind = Slot::Kind::Clock; cs.key = wid; out.push_back(std::move(cs));
    } else if (eh::config::widget_implementation_type(wid) == "weather") {
      Slot ws; ws.kind = Slot::Kind::Weather; ws.key = wid; out.push_back(std::move(ws));
    } else if (eh::config::widget_implementation_type(wid) == "media") {
      Slot ms; ms.kind = Slot::Kind::Media; ms.key = wid; out.push_back(std::move(ms));
    } else if (eh::config::widget_implementation_type(wid) == "workspaces") {
      Slot ws; ws.kind = Slot::Kind::Workspaces; ws.key = wid; out.push_back(std::move(ws));
    } else if (eh::config::widget_implementation_type(wid) == "control_center") {
      Slot cc; cc.kind = Slot::Kind::ControlCenter; cc.key = wid; out.push_back(std::move(cc));
    } else if (wid == "trash" || eh::config::widget_implementation_type(wid) == "trash") {
      Slot ts; ts.kind = Slot::Kind::Trash; ts.key = wid; out.push_back(std::move(ts));
    } else if (eh::config::widget_implementation_type(wid) == "volume_mixer") {
      Slot vm; vm.kind = Slot::Kind::VolumeMixer; vm.key = wid; out.push_back(std::move(vm));
    } else if (eh::config::widget_implementation_type(wid) == "battery") {
      Slot bs; bs.kind = Slot::Kind::Battery; bs.key = wid; out.push_back(std::move(bs));
    } else if (eh::config::widget_implementation_type(wid) == "bluetooth") {
      Slot bts; bts.kind = Slot::Kind::Bluetooth; bts.key = wid; out.push_back(std::move(bts));
    } else if (eh::config::widget_implementation_type(wid) == "vpn") {
      Slot vs; vs.kind = Slot::Kind::Vpn; vs.key = wid; out.push_back(std::move(vs));
    } else if (eh::config::widget_implementation_type(wid) == "world_clock") {
      Slot wcs; wcs.kind = Slot::Kind::WorldClock; wcs.key = wid; out.push_back(std::move(wcs));
    }
  };

  auto build_section = [&](const std::vector<std::string>& widgets) {
    std::vector<Slot> out;
    for (const auto& w : widgets) append_widget(out, w);
    return out;
  };

  std::vector<Slot> left = build_section(leftW);
  std::vector<Slot> center = build_section(centerW);
  std::vector<Slot> right = build_section(rightW);

  std::vector<Slot> all;
  all.reserve(left.size() + center.size() + right.size());
  all.insert(all.end(), left.begin(), left.end());
  all.insert(all.end(), center.begin(), center.end());
  all.insert(all.end(), right.begin(), right.end());
  app.cachedLeft = std::move(left);
  app.cachedCenter = std::move(center);
  app.cachedRight = std::move(right);
  app.cachedAll = std::move(all);
  app.slotFpLeftW = leftW;
  app.slotFpCenterW = centerW;
  app.slotFpRightW = rightW;
  app.slotFpPinned = app.settings.pinnedApps;
  app.slotFpRunningKeys.clear();
  app.slotFpRunningSerials.clear();
  app.slotFpRunningActive.clear();
  for (const auto& g : runningGroups) {
    app.slotFpRunningKeys.push_back(g.key);
    app.slotFpRunningSerials.push_back(g.chosenSerial);
    app.slotFpRunningActive.push_back((char)g.anyActivated);
  }
  app.slotFpTrayIds.clear();
  for (const auto& t : traySnap) app.slotFpTrayIds.push_back(t.service + t.path);
  app.slotFpPinDragging = app.pinDragging;
  app.slotFpPinDragKey = app.pinDragKey;
  app.slotFpConfigGen = eh::config::shell_config_generation();
  app.cachedFloatingPinLookup = floatingPinLookup;
  app.cachedFloatingPinRunning = floatingPinRunning;
  app.cachedFloatingPinActivated = floatingPinActivated;
  app.slotsValid = true;
  }
  const std::vector<Slot>& left = app.cachedLeft;
  const std::vector<Slot>& center = app.cachedCenter;
  const std::vector<Slot>& right = app.cachedRight;
  const std::vector<Slot>& all = app.cachedAll;

  const auto& scAct = eh::config::shell_config_snapshot();
  const double globalScale = std::clamp(scAct.dock.shellUiScale, 0.5, 2.0);
  const double taskbarUIScale = std::clamp(app.settings.scale, 0.5, 2.0) * globalScale;
  const double iconRaw = static_cast<double>(app.settings.iconSize) * taskbarUIScale;
  // Clamp icon so it always fits within the bar with at least 4px breathing room
  // on each side, matching how the dock constrains bar height >= icon + 4.
  const double icon = std::clamp(iconRaw, 8.0, std::max(8.0, boxH - 8.0));
  const double gap = static_cast<double>(app.settings.iconSpacing) * taskbarUIScale;

  struct WidthKey {
    Slot::Kind kind = Slot::Kind::App;
    std::string key;
    bool operator==(const WidthKey& o) const { return kind == o.kind && key == o.key; }
  };
  struct WidthKeyHash {
    size_t operator()(const WidthKey& k) const noexcept {
      size_t h = std::hash<int>{}(static_cast<int>(k.kind));
      h ^= std::hash<std::string>{}(k.key) + 0x9e3779b9 + (h << 6) + (h >> 2);
      return h;
    }
  };
  std::unordered_map<WidthKey, double, WidthKeyHash> slotWidthCache;
  auto paint_slot_w_uncached = [&](const Slot& s) -> double {
    if (s.kind == Slot::Kind::Clock) {
      return eh::shell::dock_slot_hooks::dock_clock_slot_width(nullptr, sc, s.key, icon, boxH);
    }
    if (s.kind == Slot::Kind::Weather) {
      return eh::shell::dock_slot_hooks::dock_weather_slot_width(nullptr, sc, s.key, icon, boxH);
    }
    if (s.kind == Slot::Kind::Separator) {
      return std::max(4.0, 6.0 * taskbarUIScale);
    }
    if (s.kind == Slot::Kind::Media) {
      return eh::shell::dock_slot_hooks::dock_media_slot_width(nullptr, sc, s.key, icon, boxH, mprisSnap,
                                                               app.settings.compactMedia);
    }
    if (s.kind == Slot::Kind::Workspaces) {
      return eh::shell::dock_slot_hooks::dock_workspaces_slot_width(nullptr, sc, s.key, icon, boxH, app.workspaceStrip);
    }
    if (s.kind == Slot::Kind::ControlCenter) {
      return eh::shell::dock_slot_hooks::dock_control_center_slot_width(sc, s.key, icon, boxH);
    }
    if (s.kind == Slot::Kind::Battery) {
      return eh::shell::dock_slot_hooks::dock_battery_slot_width(nullptr, sc, s.key, icon, boxH);
    }
    if (s.kind == Slot::Kind::Bluetooth) {
      return eh::shell::dock_slot_hooks::dock_bluetooth_slot_width(nullptr, sc, s.key, icon, boxH);
    }
    if (s.kind == Slot::Kind::WorldClock) {
      return eh::shell::dock_slot_hooks::dock_world_clock_slot_width(nullptr, sc, s.key, icon, boxH);
    }
    if (s.kind == Slot::Kind::App) {
      return icon + tb_app_label_extra_w(app, s.chosenSerial, taskbarUIScale);
    }
    return icon;
  };
  auto paint_slot_w = [&](const Slot& s) -> double {
    WidthKey key{s.kind, s.key};
    auto cit = slotWidthCache.find(key);
    if (cit != slotWidthCache.end()) return cit->second;
    const double w = paint_slot_w_uncached(s);
    slotWidthCache.emplace(std::move(key), w);
    return w;
  };

  // Single source of truth for inter-slot gaps (also used by
  // taskbar_measure_sections so fill-mode bar sizing matches what paints).
  auto gap_between = [&](const Slot& a, const Slot& b) -> double {
    if (a.kind == Slot::Kind::Tray && b.kind == Slot::Kind::Tray) return 0.0;
    if (a.kind == Slot::Kind::App && a.isPinned && b.kind == Slot::Kind::App && b.isPinned)
      return app.settings.pinnedAppsTrayPill ? 0.0 : gap;
    if (a.kind == Slot::Kind::App && !a.isPinned && b.kind == Slot::Kind::App && !b.isPinned)
      return app.settings.runningAppsTrayPill ? 0.0 : gap;
    return gap;
  };

  auto gap_after_slots = [&](const std::vector<Slot>& slots, size_t idx) -> double {
    if (idx + 1 >= slots.size()) return 0.0;
    return gap_between(slots[idx], slots[idx + 1]);
  };

  auto section_width = [&](const std::vector<Slot>& slots) -> double {
    double tw = 0.0;
    for (size_t i = 0; i < slots.size(); ++i) {
      tw += paint_slot_w(slots[i]);
      if (i + 1 < slots.size()) tw += gap_after_slots(slots, i);
    }
    return tw;
  };

  // Inner padding against the bar's rounded ends plus a breathing gap between
  // the left/center/right sections (shared with the fill-mode bar sizing in
  // taskbar_draw so both agree on the geometry).
  app.sectionMs[7] += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - tC0).count();
  const double stripPad = eh::shell::taskbar::strip_pad_px(taskbarUIScale);
  const double secGapPaint = eh::shell::taskbar::section_gap_px(taskbarUIScale);
  const double stripInner = stripPad * 2.0;
  app.sectionMs[4] += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - tSetup0).count();
  const auto tW0 = std::chrono::steady_clock::now();
  double lw = section_width(left);
  double cw = section_width(center);
  double rw = section_width(right);
  app.sectionMs[3] += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - tW0).count();
  auto lr = tb_lr_strip_layout(x, boxW, stripInner, lw, cw, rw, secGapPaint);
  bool use_lr = center.empty() && lr.side_by_side;

  auto total_all_w = [&]() {
    double t = 0.0;
    for (size_t i = 0; i < all.size(); i++) {
      t += paint_slot_w(all[i]);
      if (i + 1 < all.size()) t += gap_after_slots(all, i);
    }
    return t;
  };
  double totalW = use_lr ? (lw + secGapPaint + rw) : total_all_w();

  // Win7 shrink-to-fit + overflow chevron for labeled strips: narrow labels
  // toward a minimum width first; if buttons still don't fit, hide trailing
  // running buttons behind a chevron slot instead of compressing the strip
  // (compression would stretch/squish the text).
  // (visCenter/visAll + pCenter/pAll are declared with the chevron pass
  // below, after the panel-collision checks.)
  app.effLabelMaxW = 0.0;
  if (app.layerOverflow.size() < app.layers.size()) app.layerOverflow.resize(app.layers.size());
  std::vector<std::pair<uint64_t, std::string>>& layerOverflow =
      app.layerOverflow[static_cast<size_t>(std::max(0, layerIdx))];
  layerOverflow.clear();
  const double availW = (app.effShowLabels && boxW > 0.0) ? std::max(0.0, boxW - stripInner) : 0.0;
  if (app.effShowLabels && boxW > 0.0 && totalW > availW) {
    int nLabeled = 0;
    for (const auto& s : all) {
      if (s.kind == Slot::Kind::App && !tb_app_slot_label(app, s.chosenSerial).empty())
        ++nLabeled;
    }
    if (nLabeled > 0 && !use_lr) {
      const double overflow = totalW - availW;
      app.effLabelMaxW = std::max(tb_label_min_w(taskbarUIScale),
          tb_label_max_w(app, taskbarUIScale) - overflow / static_cast<double>(nLabeled));
      slotWidthCache.clear();
      lw = section_width(left);
      cw = section_width(center);
      rw = section_width(right);
      lr = tb_lr_strip_layout(x, boxW, stripInner, lw, cw, rw, secGapPaint);
      use_lr = center.empty() && lr.side_by_side;
      totalW = use_lr ? (lw + secGapPaint + rw) : total_all_w();
    }
  }

  // Panel layout: sections anchored with inner padding — left at the padded
  // start, right at the padded end, center truly centered on the bar (and
  // therefore on the screen). The checks below downgrade to the centered
  // strip layout when the sections would collide.
  double panelLeftX = x + stripPad;
  double panelCenterX = x + (boxW - cw) / 2.0;
  double panelRightX = x + boxW - stripPad - rw;
  {
    const double pad = secGapPaint;
    const double innerL = x + stripPad;
    const double innerR = x + boxW - stripPad;
    if (!left.empty() && !right.empty() && panelLeftX + lw + pad > panelRightX) use_panel = false;
    if (use_panel && !left.empty() && !center.empty() && panelLeftX + lw + pad > panelCenterX) use_panel = false;
    if (use_panel && !center.empty() && !right.empty() && panelCenterX + cw + pad > panelRightX) use_panel = false;
    // Every section must stay inside the padded content zone.
    if (use_panel && !left.empty() && panelLeftX + lw > innerR + 1e-9) use_panel = false;
    if (use_panel && !center.empty() &&
        (panelCenterX < innerL - 1e-9 || panelCenterX + cw > innerR + 1e-9)) use_panel = false;
    if (use_panel && !right.empty() &&
        (panelRightX < innerL - 1e-9 || panelRightX + rw > innerR + 1e-9)) use_panel = false;
  }

  // Overflow chevron: with labels on and the strip still overflowing, hide
  // trailing running buttons behind a chevron slot. Panel mode truncates
  // the center section so left/right stay anchored; the centered strip
  // truncates the flat list.
  std::vector<Slot> visCenter;
  std::vector<Slot> visAll;
  const std::vector<Slot>* pCenter = &center;
  const std::vector<Slot>* pAll = &all;
  (void)pCenter;
  if (app.effShowLabels && boxW > 0.0 && totalW > std::max(0.0, boxW - stripInner)) {
    const double chevW = icon;
    std::vector<std::pair<uint64_t, std::string>> hidden;
    auto run_hidden = [&](std::vector<Slot>& vec) {
      while (vec.size() > 1) {
        double t = 0.0;
        for (size_t k = 0; k < vec.size(); k++) {
          t += paint_slot_w(vec[k]);
          if (k + 1 < vec.size()) t += gap_between(vec[k], vec[k + 1]);
        }
        if (t + gap + chevW <= std::max(0.0, boxW - stripInner)) break;
        int drop = -1;
        for (int k = static_cast<int>(vec.size()) - 1; k >= 0; --k) {
          const auto& s = vec[static_cast<size_t>(k)];
          if (s.kind == Slot::Kind::App && s.chosenSerial != 0) {
            drop = k;
            break;
          }
        }
        if (drop < 0) break;
        auto wins = tb_group_windows(app, vec[static_cast<size_t>(drop)].key,
                                     vec[static_cast<size_t>(drop)].chosenSerial);
        hidden.insert(hidden.begin(), wins.begin(), wins.end());
        vec.erase(vec.begin() + drop);
      }
    };
    if (use_panel && !center.empty()) {
      visCenter = center;
      run_hidden(visCenter);
    } else if (!use_panel && !use_lr) {
      visAll = all;
      run_hidden(visAll);
    }
    if (!hidden.empty()) {
      Slot chev;
      chev.kind = Slot::Kind::App;
      chev.key = eh::shell::taskbar::kTbChevronKey;
      chev.isPinned = false;
      if (use_panel && !center.empty()) {
        visCenter.push_back(std::move(chev));
        pCenter = &visCenter;
        cw = section_width(visCenter);
        panelCenterX = x + (boxW - cw) / 2.0;
      } else {
        visAll.push_back(std::move(chev));
        pAll = &visAll;
      }
      totalW = 0.0;
      for (size_t k = 0; k < pAll->size(); k++) {
        totalW += paint_slot_w((*pAll)[k]);
        if (k + 1 < pAll->size()) totalW += gap_between((*pAll)[k], (*pAll)[k + 1]);
      }
      layerOverflow = std::move(hidden);
    }
  }

  const double iconY = y + (boxH - icon) / 2.0;

  bool mediaMarqueeAccum = false;

  auto render_section = [&](const std::vector<Slot>& slots, double x0, int slotIndexBase) {
    auto gap_local = [&](size_t idx) -> double {
      if (idx + 1 >= slots.size()) return 0.0;
      return gap_between(slots[idx], slots[idx + 1]);
    };

    auto slot_lift = [&](size_t idx) -> double {
      const int gIdx = static_cast<int>(idx) + slotIndexBase;
      double ly = 0.0;
      if (gIdx == taskbarHoverSlot && slots[idx].kind != Slot::Kind::Media &&
          slots[idx].kind != Slot::Kind::WorldClock &&
          slots[idx].kind != Slot::Kind::Clock && slots[idx].kind != Slot::Kind::Weather &&
          slots[idx].kind != Slot::Kind::ControlCenter && slots[idx].kind != Slot::Kind::Separator &&
          slots[idx].kind != Slot::Kind::Settings && slots[idx].kind != Slot::Kind::Spotlight &&
          slots[idx].kind != Slot::Kind::AppDrawer && slots[idx].kind != Slot::Kind::Smenu &&
          slots[idx].kind != Slot::Kind::Trash && slots[idx].kind != Slot::Kind::AppMenu &&
          slots[idx].kind != Slot::Kind::Tray && slots[idx].kind != Slot::Kind::Workspaces &&
          slots[idx].kind != Slot::Kind::VolumeMixer && slots[idx].kind != Slot::Kind::Vpn &&
          slots[idx].kind != Slot::Kind::Battery &&
          slots[idx].kind != Slot::Kind::Bluetooth) ly -= taskbarHoverLiftPx;
      if (gIdx == taskbarPressedSlot && slots[idx].kind == Slot::Kind::App) ly -= kPressLiftPx;
      // Launch feedback: the launching/activating app slot hops (damped sine,
      // dock parity). Widget slots never bounce — they don't launch apps.
      if (slots[idx].kind == Slot::Kind::App) ly += taskbar_launch_bounce_lift_y(app, slots[idx].key);
      return ly;
    };

    auto min_lift = [&](size_t from, size_t to_inclusive) -> double {
      double m = 0.0;
      for (size_t j = from; j <= to_inclusive; ++j) m = std::min(m, slot_lift(j));
      return m;
    };

    double curX = x0;
    for (size_t i = 0; i < slots.size(); i++) {
      const auto& s = slots[i];
      const double slotW = paint_slot_w(s);
      const double ix = curX;
      // Path hygiene: strokes/fills must never replay stale subpaths from
      // previous slots (leftover rounded rects re-stroke as stray lines).
      cairo_new_path(cr);
      if (out_hits) {
        out_hits->push_back({s.key, ix, iconY, slotW, icon, s.chosenSerial, s.isPinned, static_cast<int>(s.kind)});
      }
      curX += slotW + gap_local(i);

      const int globalIdx = static_cast<int>(i) + slotIndexBase;
      const bool hovered = (taskbarHoverSlot >= 0 && globalIdx == taskbarHoverSlot);
      // Raw press flag: the media / workspaces / control-center hooks take it
      // as-is for their own internal feedback (transport buttons, cells, …).
      const bool pressed = (taskbarPressedSlot >= 0 && globalIdx == taskbarPressedSlot);
      // Dock parity: the generic slot press overlay (outline + press scale +
      // press lift) only lights up app slots. Widget slots are wide and paint
      // their own feedback, so an icon-sized box over them reads wrong.
      const bool slotPressed = pressed && s.kind == Slot::Kind::App;
      const double liftY = slot_lift(i);
      auto hover_overlay = [&] {
        const double r = eh::shell::dock_slot_hooks::slot_pill::corner_radius(icon, slotW);
        rounded_rect(ix, iconY + liftY, slotW, icon, r);
        cairo_set_source_rgba(cr, eh::widgets::slot_pill_style::g_hoverAccentR, eh::widgets::slot_pill_style::g_hoverAccentG, eh::widgets::slot_pill_style::g_hoverAccentB, 0.18);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, eh::widgets::slot_pill_style::g_hoverAccentR, eh::widgets::slot_pill_style::g_hoverAccentG, eh::widgets::slot_pill_style::g_hoverAccentB, 0.35);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
      };
      const double pressS = slotPressed ? kPressIconScale : 1.0;
      const bool usePress = (pressS < 0.999);

      if (s.kind == Slot::Kind::Separator) {
        cairo_set_source_rgba(cr, eh::widgets::slot_pill_style::g_pillR, eh::widgets::slot_pill_style::g_pillG, eh::widgets::slot_pill_style::g_pillB, 0.35);
        cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, icon * 0.55);
        cairo_text_extents_t te;
        cairo_text_extents(cr, "|", &te);
        cairo_move_to(cr, ix + (slotW - te.width) / 2.0 - te.x_bearing,
                      iconY + (icon - te.height) / 2.0 - te.y_bearing);
        cairo_show_text(cr, "|");
        continue;
      }

      if (slotPressed) {
        // Press feedback hugs the icon square so the glyph keeps equal
        // padding on all four sides, labeled or not.
        rounded_rect(ix - 2.0, iconY + liftY - 2.0, icon + 4.0, icon + 4.0, 14.0);
        cairo_set_source_rgba(cr, 1.00, 1.00, 1.00, 0.12);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0.55, 0.80, 1.00, 0.58);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
      }

      if (usePress) {
        const double pcx = ix + slotW * 0.5;
        const double pcy = iconY + liftY + icon * 0.5;
        cairo_save(cr);
        cairo_translate(cr, pcx, pcy);
        cairo_scale(cr, pressS, pressS);
        cairo_translate(cr, -pcx, -pcy);
      }

      bool drew = false;

      if (s.kind == Slot::Kind::Settings) {
        {
          eh::widgets::slot_pill_style::paint_pill(cr, ix, iconY + liftY, slotW, icon);
        }
        if (hovered) hover_overlay();
        if (!app.settingsLogo) app.settingsLogo = eh::icons::IconCache::load_settings_logo_surface();
        if (app.settingsLogo) {
          const int lw_img = cairo_image_surface_get_width(app.settingsLogo);
          const int lh_img = cairo_image_surface_get_height(app.settingsLogo);
          const double pad2 = 6.0;
          const double avail = icon - pad2 * 2.0;
          const double sc_logo = avail / std::max(1, std::max(lw_img, lh_img));
          const double dw = lw_img * sc_logo;
          const double dh = lh_img * sc_logo;
          const double dx = ix + (icon - dw) / 2.0;
          const double dy = iconY + liftY + (icon - dh) / 2.0;
          eh_os_logo::paint_logo_surface_scaled(cr, app.settingsLogo, dx, dy, sc_logo, matugen,
                                                mc.accentR, mc.accentG, mc.accentB);
          drew = true;
        }
      } else if (s.kind == Slot::Kind::Spotlight || s.kind == Slot::Kind::AppDrawer) {
        if (hovered) hover_overlay();
        if (!app.distroSpotlightLogo) app.distroSpotlightLogo = eh_os_logo::load_distro_logo_cairo_surface();
        if (app.distroSpotlightLogo) {
          const int lw_img = cairo_image_surface_get_width(app.distroSpotlightLogo);
          const int lh_img = cairo_image_surface_get_height(app.distroSpotlightLogo);
          const double pad2 = 6.0;
          const double avail = icon - pad2 * 2.0;
          const double sc_logo = avail / std::max(1, std::max(lw_img, lh_img));
          const double dw = lw_img * sc_logo;
          const double dh = lh_img * sc_logo;
          const double dx = ix + (icon - dw) / 2.0;
          const double dy = iconY + liftY + (icon - dh) / 2.0;
          eh_os_logo::paint_logo_surface_scaled(cr, app.distroSpotlightLogo, dx, dy, sc_logo, matugen,
                                                mc.accentR, mc.accentG, mc.accentB);
          drew = true;
        }
      } else if (s.kind == Slot::Kind::Trash) {
        if (hovered) hover_overlay();
        if (app.trashFull == 0) {
          const char* home = std::getenv("HOME");
          if (home) {
            const std::string trashPath = std::string(home) + "/.local/share/Trash/files";
            DIR* d = opendir(trashPath.c_str());
            if (d) {
              bool hasFiles = false;
              struct dirent* de;
              while ((de = readdir(d)) != nullptr) {
                const std::string name(de->d_name);
                if (name != "." && name != "..") { hasFiles = true; break; }
              }
              closedir(d);
              app.trashFull = hasFiles;
            } else { app.trashFull = false; }
          } else { app.trashFull = false; }
        }
        cairo_surface_t* trashLogo = app.trashFull ? app.trashFullLogo : app.trashEmptyLogo;
        if (!trashLogo) {
          if (app.trashFull) {
            if (!app.trashFullLogo) app.trashFullLogo = eh::shell::asset::load_trash_full_surface();
            trashLogo = app.trashFullLogo;
          } else {
            if (!app.trashEmptyLogo) app.trashEmptyLogo = eh::shell::asset::load_trash_empty_surface();
            trashLogo = app.trashEmptyLogo;
          }
        }
        if (trashLogo) {
          const int lw_img = cairo_image_surface_get_width(trashLogo);
          const int lh_img = cairo_image_surface_get_height(trashLogo);
          const double pad2 = 6.0;
          const double avail = icon - pad2 * 2.0;
          const double sc_logo = avail / std::max(1, std::max(lw_img, lh_img));
          const double dw = lw_img * sc_logo;
          const double dh = lh_img * sc_logo;
          const double dx = ix + (icon - dw) / 2.0;
          const double dy = iconY + liftY + (icon - dh) / 2.0;
          eh_os_logo::paint_logo_surface_scaled(cr, trashLogo, dx, dy, sc_logo, false, 0, 0, 0);
          drew = true;
        }
      } else if (s.kind == Slot::Kind::AppMenu) {
        if (hovered) hover_overlay();
        const double gx = ix + icon * 0.5;
        const double gy = iconY + liftY + icon * 0.5;
        const double pitch = icon / 6.8;
        const double rDot = icon * 0.06;
        if (matugen)
          cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 1.0);
        else
          cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        for (int ry = -1; ry <= 1; ++ry) {
          for (int sx = -1; sx <= 1; ++sx) {
            cairo_arc(cr, gx + static_cast<double>(sx) * pitch, gy + static_cast<double>(ry) * pitch, rDot, 0, 2 * M_PI);
            cairo_fill(cr);
          }
        }
        drew = true;
      } else if (s.kind == Slot::Kind::Smenu) {
        if (hovered) hover_overlay();
        if (!app.distroSpotlightLogo) app.distroSpotlightLogo = eh_os_logo::load_distro_logo_cairo_surface();
        if (app.distroSpotlightLogo) {
          const int lw_img = cairo_image_surface_get_width(app.distroSpotlightLogo);
          const int lh_img = cairo_image_surface_get_height(app.distroSpotlightLogo);
          const double pad2 = 6.0;
          const double avail = icon - pad2 * 2.0;
          const double sc_logo = avail / std::max(1, std::max(lw_img, lh_img));
          const double dw = lw_img * sc_logo;
          const double dh = lh_img * sc_logo;
          const double dx = ix + (icon - dw) / 2.0;
          const double dy = iconY + liftY + (icon - dh) / 2.0;
          eh_os_logo::paint_logo_surface_scaled(cr, app.distroSpotlightLogo, dx, dy, sc_logo, matugen,
                                                mc.accentR, mc.accentG, mc.accentB);
          drew = true;
        }
      } else if (s.kind == Slot::Kind::Tray) {
        const auto* ti = find_tray_item(traySnap, s.key);
        const bool firstTray = (i == 0 || slots[i - 1].kind != Slot::Kind::Tray);
        if (firstTray) {
          size_t runEnd = i;
          while (runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::Tray) runEnd++;
          const int nRun = static_cast<int>(runEnd - i + 1);
          const double runW = static_cast<double>(nRun) * icon;
          const double runLiftY = min_lift(i, runEnd);
          eh::widgets::slot_pill_style::paint_pill(cr, ix, iconY + runLiftY, runW, icon);
        }
        if (hovered) hover_overlay();
        if (ti) {
          const double maxSide = std::min(icon * 0.6, icon - 8.0);
          const double avail = std::max(4.0, maxSide);
          if (!ti->iconName.empty()) {
            if (const auto* ic = paint_tray_icon(app.icons, ti->iconName)) {
              if (ic->surface) {
                const double sx = avail / std::max(1, ic->width);
                const double sy = avail / std::max(1, ic->height);
                const double sc_i = std::min(sx, sy);
                const double dw = ic->width * sc_i;
                const double dh = ic->height * sc_i;
                const double dx = ix + (icon - dw) / 2.0;
                const double dy = iconY + liftY + (icon - dh) / 2.0;
                /* Blit the pre-scaled raster instead of scaling the 384px source
                   per frame; see TaskbarScaledIconCache. */
                const int tw = std::max(1, static_cast<int>(std::ceil(dw)));
                const int th = std::max(1, static_cast<int>(std::ceil(dh)));
                cairo_surface_t* scaled = app.scaledIcons.getOrScale(ic->surface, tw, th);
                cairo_set_source_surface(cr, scaled ? scaled : ic->surface, dx, dy);
                cairo_paint(cr);
                drew = true;
              }
            }
          } else if (ti->pixSurface) {
            const int lw_img = cairo_image_surface_get_width(ti->pixSurface);
            const int lh_img = cairo_image_surface_get_height(ti->pixSurface);
            const double sx = avail / std::max(1, lw_img);
            const double sy = avail / std::max(1, lh_img);
            const double sc_i = std::min(sx, sy);
            const double dw = lw_img * sc_i;
            const double dh = lh_img * sc_i;
            const double dx = ix + (icon - dw) / 2.0;
            const double dy = iconY + liftY + (icon - dh) / 2.0;
            const int tw = std::max(1, static_cast<int>(std::ceil(dw)));
            const int th = std::max(1, static_cast<int>(std::ceil(dh)));
            cairo_surface_t* scaled = app.scaledIcons.getOrScale(ti->pixSurface, tw, th);
            cairo_set_source_surface(cr, scaled ? scaled : ti->pixSurface, dx, dy);
            cairo_paint(cr);
            drew = true;
          } else {
            const std::string mk = ti->service + ti->path;
            if (!app.trayMissingLogged.contains(mk)) {
              app.trayMissingLogged[mk] = true;
            }
          }
        }
      } else if (s.kind == Slot::Kind::WorldClock) {
        eh::shell::dock_slot_hooks::paint_world_clock_slot(cr, sc, s.key, ix, iconY + liftY, slotW, icon, icon, false, false);
        if (hovered) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::Clock) {
        eh::shell::dock_slot_hooks::paint_clock_slot(cr, sc, s.key, ix, iconY + liftY, slotW, icon, icon, false, false);
        if (hovered) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::Weather) {
        eh::shell::dock_slot_hooks::paint_weather_slot(cr, sc, s.key, ix, iconY + liftY, slotW, icon, icon, false, false);
        if (hovered) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::Media) {
        const int mediaHoverBtn = hovered ? eh::mpris::DockMpris::media_hit_zone(app.pointerX - ix, slotW, icon) : -1;
        if (eh::shell::dock_slot_hooks::paint_media_slot(cr, sc, s.key, ix, iconY + liftY, slotW, icon, icon, mprisSnap, hovered, pressed, mediaHoverBtn, app.pointerX, app.pointerY, nullptr, app.settings.compactMedia))
          mediaMarqueeAccum = true;
        drew = true;
      } else if (s.kind == Slot::Kind::Workspaces) {
        if (eh::shell::dock_slot_hooks::paint_workspaces_slot(cr, sc, s.key, ix, iconY + liftY, slotW, icon, icon, app.workspaceStrip, hovered, pressed, &app.icons, &app.wsStripAnim)) {
          // animation active, will be picked up by next frame
        }
        if (hovered) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::ControlCenter) {
        (void)eh::shell::dock_slot_hooks::paint_control_center_slot(cr, sc, s.key, ix, iconY + liftY, slotW, icon, icon, hovered, pressed);
        if (hovered) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::Battery) {
        eh::shell::dock_slot_hooks::paint_battery_slot(cr, sc, s.key, ix, iconY + liftY, slotW, icon, icon, false, false);
        if (hovered) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::Bluetooth) {
        eh::shell::dock_slot_hooks::paint_bluetooth_slot(cr, sc, s.key, ix, iconY + liftY, slotW, icon, icon, false, false);
        if (hovered) hover_overlay();
        drew = true;
      } else if (s.kind == Slot::Kind::VolumeMixer) {
        {
          eh::widgets::slot_pill_style::paint_pill(cr, ix, iconY + liftY, slotW, icon);
        }
        if (hovered) hover_overlay();
        const double gx = ix + icon * 0.5;
        const double gy = iconY + liftY + icon * 0.5;
        if (matugen)
          eh::shell::draw_material_glyph(cr, gx, gy, icon * 0.52, "volume_up", mc.accentR, mc.accentG, mc.accentB, 1.0);
        else
          eh::shell::draw_material_glyph(cr, gx, gy, icon * 0.52, "volume_up", 1.0, 1.0, 1.0, 1.0);
        drew = true;
      } else if (s.kind == Slot::Kind::Vpn) {
        {
          eh::widgets::slot_pill_style::paint_pill(cr, ix, iconY + liftY, slotW, icon);
        }
        if (hovered) hover_overlay();
        const double gx = ix + icon * 0.5;
        const double gy = iconY + liftY + icon * 0.5;
        if (matugen)
          eh::shell::draw_material_glyph(cr, gx, gy, icon * 0.52, "vpn_key", mc.accentR, mc.accentG, mc.accentB, 1.0);
        else
          eh::shell::draw_material_glyph(cr, gx, gy, icon * 0.52, "vpn_key", 1.0, 1.0, 1.0, 1.0);
        drew = true;
      } else if (s.kind == Slot::Kind::App) {
        if (s.key == eh::shell::taskbar::kTbChevronKey) {
          // Overflow chevron: three dots opening the hidden-windows popup.
          eh::widgets::slot_pill_style::paint_pill(cr, ix, iconY + liftY, slotW, icon);
          if (hovered) hover_overlay();
          cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
          const double dotR = std::max(1.5, icon * 0.055);
          const double cyDot = iconY + liftY + icon * 0.5;
          for (int d = -1; d <= 1; ++d) {
            cairo_arc(cr, ix + slotW * 0.5 + d * dotR * 3.0, cyDot, dotR, 0, 2 * M_PI);
            cairo_fill(cr);
          }
          drew = true;
        } else {
        // Labeled buttons keep the exact icon geometry of icon-only mode:
        // the glyph stays centered in its square cell with equal padding.
        // (No wide persistent backdrop — the label reads directly off it.)
        // Stacked-group indicator (modern take on Win7's layered button
        // edges): extra glass sheets peeking from behind slots that hold
        // more than one window. Paint-only: the front icon stays centered
        // in its box and neighbors never move.
        const int stackCount = tb_group_window_count(app, s.key, s.chosenSerial);
        const bool showStack = stackCount > 1;
        if (showStack) {
          const int sheets = std::min(stackCount - 1, 2);
          for (int sh = sheets; sh >= 1; --sh) {
            const double ox = 3.0 * sh * taskbarUIScale;
            const double oy = -3.0 * sh * taskbarUIScale;
            const double sr = eh::shell::dock_slot_hooks::slot_pill::corner_radius(icon, slotW);
            rounded_rect(ix + ox, iconY + liftY + oy, slotW, icon, sr);
            cairo_set_source_rgba(cr, 1, 1, 1, sh == sheets ? 0.16 : 0.11);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.32);
            cairo_set_line_width(cr, 1.25);
            cairo_stroke(cr);
          }
        }
        // Win7 tasklist button face: glass only while the app is open.
        // Unopened pins stay bare (icon only, no box).
        if (s.win7 && s.chosenSerial != 0) {
          const double br = eh::shell::dock_slot_hooks::slot_pill::corner_radius(icon, slotW);
          rounded_rect(ix, iconY + liftY, slotW, icon, br);
          {
            const bool focused = s.anyActivated;
            cairo_set_source_rgba(cr, 1, 1, 1, focused ? 0.22 : 0.13);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, focused ? 0.45 : 0.22);
            cairo_set_line_width(cr, focused ? 1.5 : 1.0);
            cairo_stroke(cr);
            // Glass top highlight, Aero-button style.
            cairo_save(cr);
            rounded_rect(ix + 1.5, iconY + liftY + 1.5, slotW - 3.0, icon * 0.45, br * 0.7);
            cairo_clip(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, focused ? 0.14 : 0.08);
            cairo_paint(cr);
            cairo_restore(cr);
            if (focused) {
              // Focused edge glow in the bar accent color.
              rounded_rect(ix - 1.0, iconY + liftY - 1.0, slotW + 2.0, icon + 2.0, br + 1.0);
              cairo_set_source_rgba(cr, eh::widgets::slot_pill_style::g_hoverAccentR,
                                    eh::widgets::slot_pill_style::g_hoverAccentG,
                                    eh::widgets::slot_pill_style::g_hoverAccentB, 0.35);
              cairo_set_line_width(cr, 1.5);
              cairo_stroke(cr);
            }
          }
        }
        // Pill outline behind consecutive pinned apps
        if (s.isPinned && app.settings.pinnedAppsTrayPill && !s.win7) {
          const bool firstPinnedInRun = (i == 0 || slots[i - 1].kind != Slot::Kind::App || !slots[i - 1].isPinned || slots[i - 1].win7);
          if (firstPinnedInRun) {
            size_t runEnd = i;
            while (runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::App && slots[runEnd + 1].isPinned && !slots[runEnd + 1].win7)
              runEnd++;
            const bool appendRightTrash = runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::Trash;
            double pillX = ix;
            double runW = 0.0;
            for (size_t k = i; k <= runEnd; ++k) runW += paint_slot_w(slots[k]);
            if (appendRightTrash) {
              const double g = gap_local(runEnd);
              runW += g + icon;
            }
            eh::widgets::slot_pill_style::paint_pill(cr, pillX, iconY, runW, icon);
          }
        }
        // Pill outline behind consecutive running apps
        if (!s.isPinned && app.settings.runningAppsTrayPill && !s.win7) {
          const bool firstRunningInRun = (i == 0 || slots[i - 1].kind != Slot::Kind::App || slots[i - 1].isPinned || slots[i - 1].win7);
          if (firstRunningInRun) {
            size_t runEnd = i;
            while (runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::App && !slots[runEnd + 1].isPinned && !slots[runEnd + 1].win7)
              runEnd++;
            const bool appendRightTrash = runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::Trash;
            double pillX = ix;
            double runW = 0.0;
            for (size_t k = i; k <= runEnd; ++k) runW += paint_slot_w(slots[k]);
            if (appendRightTrash) {
              const double g = gap_local(runEnd);
              runW += g + icon;
            }
            eh::widgets::slot_pill_style::paint_pill(cr, pillX, iconY, runW, icon);
          }
        }
        const std::string lookup = !s.iconId.empty() ? s.iconId : s.key;
        const std::string slotLabel = tb_app_slot_label(app, s.chosenSerial);
        const bool labeled = !slotLabel.empty();
        if (hovered) {
          // Color Hot-track: running buttons glow in the icon's dominant
          // color, centered on the cursor. Anything else keeps the flat
          // generic highlight.
          double hr = 0.55, hg = 0.75, hb = 1.0;
          bool hot = false;
          if (s.chosenSerial != 0 && app.toplevels &&
              toplevel_by_serial(*app.toplevels, s.chosenSerial)) {
            hot = true;
            tb_hot_track_color(app, lookup, &hr, &hg, &hb);
          }
          if (hot) {
            const double pcx = std::clamp(app.pointerX - ix, 0.0, slotW);
            const double pcy = icon * 0.5;
            const double prad = std::max(slotW, icon) * 0.9;
            const double hrad = eh::shell::dock_slot_hooks::slot_pill::corner_radius(icon, slotW);
            rounded_rect(ix, iconY + liftY, slotW, icon, hrad);
            cairo_pattern_t* pat = cairo_pattern_create_radial(
                ix + pcx, iconY + liftY + pcy, 1.0, ix + pcx, iconY + liftY + pcy, prad);
            cairo_pattern_add_color_stop_rgba(pat, 0.0, 1, 1, 1, 0.38);
            cairo_pattern_add_color_stop_rgba(pat, 0.35, hr, hg, hb, 0.55);
            cairo_pattern_add_color_stop_rgba(pat, 1.0, hr, hg, hb, 0.0);
            cairo_set_source(cr, pat);
            cairo_fill_preserve(cr);
            cairo_pattern_destroy(pat);
            cairo_set_source_rgba(cr, hr, hg, hb, 0.55);
            cairo_set_line_width(cr, 1.25);
            cairo_stroke(cr);
          } else if (s.win7) {
            hover_overlay();
          } else if (!s.isPinned) {
            const double cellRad = eh::shell::dock_slot_hooks::slot_pill::corner_radius(icon, icon);
            rounded_rect(ix, iconY + liftY, icon, icon, cellRad);
            eh::shell::dock_slot_hooks::slot_pill::set_fill_for_state(cr, true, false);
            cairo_fill(cr);
          }
        }
        if (const auto* ic = paint_app_icon(app.icons, lookup)) {
          if (ic->surface) {
            const double pad2 = 6.0;
            const double avail = icon - pad2 * 2.0;
            const double sx = avail / std::max(1, ic->width);
            const double sy = avail / std::max(1, ic->height);
            const double sc_i = std::min(sx, sy);
            const double dw = ic->width * sc_i;
            const double dh = ic->height * sc_i;
            // Icon stays centered in its cell, labeled or not.
            const double dx = ix + (icon - dw) / 2.0;
            const double dy = iconY + liftY + (icon - dh) / 2.0;
            const int tw = std::max(1, static_cast<int>(std::ceil(dw)));
            const int th = std::max(1, static_cast<int>(std::ceil(dh)));
            cairo_surface_t* scaled = app.scaledIcons.getOrScale(ic->surface, tw, th);
            cairo_set_source_surface(cr, scaled ? scaled : ic->surface, dx, dy);
            cairo_paint(cr);
            drew = true;
          }
        }
        if (labeled) {
          const double fontPx = tb_label_font_px(taskbarUIScale);
          const double maxW = tb_label_max_w(app, taskbarUIScale);
          const std::string shown = tb_label_ellipsize(app, slotLabel, fontPx, maxW);
          const double tx = ix + icon + 6.0 * taskbarUIScale;
          cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
          cairo_set_font_size(cr, fontPx);
          cairo_text_extents_t te{};
          cairo_text_extents(cr, shown.c_str(), &te);
          cairo_set_source_rgba(cr, 1, 1, 1, 0.92);
          cairo_move_to(cr, tx - te.x_bearing,
                        iconY + liftY + (icon - te.height) / 2.0 - te.y_bearing);
          cairo_show_text(cr, shown.c_str());
          drew = true;
        }
        if (s.win7) {
          // Window-count badge, top-right: how many windows share this button.
          const int nwin = tb_group_window_count(app, s.key, s.chosenSerial);
          if (nwin > 1) {
            const std::string num = std::to_string(nwin);
            const double bR = std::max(8.0, 8.5 * taskbarUIScale);
            const double bcx = ix + slotW - 3.0 * taskbarUIScale;
            const double bcy = iconY + liftY + 3.0 * taskbarUIScale;
            cairo_new_path(cr);
            cairo_arc(cr, bcx, bcy, bR, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, 0.10, 0.11, 0.13, 0.92);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);
            cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, std::max(9.0, 10.5 * taskbarUIScale));
            cairo_text_extents_t be{};
            cairo_text_extents(cr, num.c_str(), &be);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
            cairo_move_to(cr, bcx - be.width / 2.0 - be.x_bearing,
                          bcy - be.height / 2.0 - be.y_bearing);
            cairo_show_text(cr, num.c_str());
          }
        }
        }  // !chevron (grouped body)
      }

      if (!drew) {
        rounded_rect(ix, iconY + liftY, slotW, icon, 12.0);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
        cairo_fill(cr);
      }

      if (usePress) cairo_restore(cr);

      // Run indicator dots
      const double iconBottom = iconY + liftY + icon;
      const double dotR = 3.0;
      const double dotCy = std::min(iconBottom + kRunIndicatorGapPx + dotR, y + boxH - dotR);
      if (s.kind == Slot::Kind::App && !s.win7 && app.toplevels && toplevel_by_serial(*app.toplevels, s.chosenSerial)) {
        cairo_new_path(cr);
        cairo_arc(cr, ix + slotW / 2.0, dotCy, dotR, 0, 2 * M_PI);
        if (s.anyActivated) cairo_set_source_rgba(cr, 0.4, 0.7, 1.0, 0.95);
        else cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
        cairo_fill(cr);
      } else if (s.kind == Slot::Kind::Settings && app.toplevels && toplevel_by_serial(*app.toplevels, s.chosenSerial)) {
        cairo_arc(cr, ix + icon / 2.0, dotCy, dotR, 0, 2 * M_PI);
        if (s.anyActivated) cairo_set_source_rgba(cr, 0.4, 0.7, 1.0, 0.95);
        else cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
        cairo_fill(cr);
      }
    }
  };

  if (use_panel) {
    const auto tL0 = std::chrono::steady_clock::now();
    render_section(left, panelLeftX, 0);
    app.sectionMs[0] += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tL0).count();
    const auto tC0 = std::chrono::steady_clock::now();
    render_section(*pCenter, panelCenterX, static_cast<int>(left.size()));
    app.sectionMs[1] += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tC0).count();
    const auto tR0 = std::chrono::steady_clock::now();
    render_section(right, panelRightX, static_cast<int>(left.size() + pCenter->size()));
    app.sectionMs[2] += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tR0).count();
  } else {
    const double midX = x + boxW * 0.5;
    const double hScale = tb_strip_h_scale(boxW, totalW, stripInner);
    // Keep the strip at least stripPad from both ends: anchor it there when
    // compression is active, otherwise keep it centered (tb_strip_h_scale only
    // allows widths that fit the padded zone, so the clamp never truncates).
    const double startX = hScale < 1.0 - 1e-9
        ? midX + ((x + stripPad) - midX) / hScale
        : x + std::max(stripPad, (boxW - totalW) * 0.5);
    if (hScale < 1.0 - 1e-9) {
      cairo_save(cr);
      cairo_translate(cr, midX, 0.0);
      cairo_scale(cr, hScale, 1.0);
      cairo_translate(cr, -midX, 0.0);
      if (use_lr) {
        render_section(left, lr.x_left, 0);
        render_section(right, lr.x_right, static_cast<int>(left.size()));
      } else {
        render_section(*pAll, startX, 0);
      }
      cairo_restore(cr);
      // The compression transform only touches x: map hit x/w the same way.
      if (out_hits) {
        for (auto& hh : *out_hits) {
          hh.x = midX + (hh.x - midX) * hScale;
          hh.w = hh.w * hScale;
        }
      }
    } else {
      if (use_lr) {
        render_section(left, lr.x_left, 0);
        render_section(right, lr.x_right, static_cast<int>(left.size()));
      } else {
        render_section(*pAll, startX, 0);
      }
    }
  }

  // Floating pin rendering for drag-to-reorder
  if (app.pinDragging && !app.cachedFloatingPinLookup.empty()) {
    const double fx = std::max(x + 4.0, std::min(x + boxW - icon - 4.0, app.pointerX - icon / 2.0));
    const double fy = iconY - 4.0;
    rounded_rect(fx - 2.0, fy - 2.0, icon + 4.0, icon + 4.0, 14.0);
    cairo_set_source_rgba(cr, 1.00, 1.00, 1.00, 0.12);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.55, 0.80, 1.00, 0.65);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    if (const auto* ic = paint_app_icon(app.icons, app.cachedFloatingPinLookup)) {
      if (ic->surface) {
        const double pad2 = 6.0;
        const double avail = icon - pad2 * 2.0;
        const double sx = avail / std::max(1, ic->width);
        const double sy = avail / std::max(1, ic->height);
        const double sc_i = std::min(sx, sy);
        const double dw = ic->width * sc_i;
        const double dh = ic->height * sc_i;
        const double dx = fx + (icon - dw) / 2.0;
        const double dy = fy + (icon - dh) / 2.0;
        const int tw = std::max(1, static_cast<int>(std::ceil(dw)));
        const int th = std::max(1, static_cast<int>(std::ceil(dh)));
        cairo_surface_t* scaled = app.scaledIcons.getOrScale(ic->surface, tw, th);
        cairo_set_source_surface(cr, scaled ? scaled : ic->surface, dx, dy);
        cairo_paint(cr);
      }
    }
    if (app.cachedFloatingPinRunning) {
      const double fBottom = fy + icon;
      const double fDotR = 3.0;
      const double fDotCy = fBottom + kRunIndicatorGapPx + fDotR;
      cairo_arc(cr, fx + icon / 2.0, fDotCy, fDotR, 0, 2 * M_PI);
      if (app.cachedFloatingPinActivated) cairo_set_source_rgba(cr, 0.4, 0.7, 1.0, 0.95);
      else cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
      cairo_fill(cr);
    }
  }

  // Per-layer diagnostics for the DP-all layout log (log-on-change in draw).
  if (layerIdx >= 0) {
    if (app.layerDiag.size() <= static_cast<size_t>(layerIdx))
      app.layerDiag.resize(static_cast<size_t>(layerIdx) + 1);
    auto& dg = app.layerDiag[static_cast<size_t>(layerIdx)];
    dg.boxW = static_cast<int>(boxW);
    dg.boxH = static_cast<int>(boxH);
    dg.nSlots = out_hits ? out_hits->size() : all.size();
    dg.totalW = totalW;
    dg.availW = std::max(0.0, boxW - stripInner);
    dg.usePanel = use_panel;
    dg.useLr = use_lr;
    dg.hScale = tb_strip_h_scale(boxW, totalW, stripInner);
    dg.labels = app.effShowLabels;
    dg.effMaxW = app.effLabelMaxW;
    dg.overflowN = (static_cast<size_t>(std::max(0, layerIdx)) < app.layerOverflow.size())
                       ? app.layerOverflow[static_cast<size_t>(std::max(0, layerIdx))].size()
                       : 0;
    dg.hitsN = out_hits ? out_hits->size() : 0;
    // Overlap detector: consecutive hits in one section must not interleave.
    // Logs (on change) the offending slots with x/w so layout vs paint width
    // mismatches show up directly.
    if (out_hits && out_hits->size() >= 2) {
      std::string sig;
      for (size_t oi = 0; oi + 1 < out_hits->size() && oi < 30; ++oi) {
        const auto& a = (*out_hits)[oi];
        const auto& b = (*out_hits)[oi + 1];
        if (a.x + a.w > b.x + 0.5) {
          char buf[192];
          std::snprintf(buf, sizeof(buf), " [%s@%.0f+%.0f overlaps %s@%.0f]",
                        a.widgetId.c_str(), a.x, a.w, b.widgetId.c_str(), b.x);
          sig += buf;
          if (sig.size() > 900) break;
        }
      }
      static std::string lastOverlap;
      const std::string key = std::to_string(layerIdx) + sig;
      if (!sig.empty() && key != lastOverlap) {
        lastOverlap = key;
        debug_log("taskbar", "dp-overlap layer=%d%s", layerIdx, sig.c_str());
      } else if (sig.empty() && !lastOverlap.empty() &&
                 lastOverlap.rfind(std::to_string(layerIdx), 0) == 0) {
        lastOverlap.clear();
      }
    }
  }
}

}
