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
#include "services/tray/filter/tray_env_filter.hpp"
#include "configuration/shell_config.hpp"

#include <cairo/cairo.h>

#include <unordered_set>

#include <algorithm>

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
  };
  std::vector<Slot> pinnedSlots, runningSlots, traySlots;
  pinnedSlots.resize(app.settings.pinnedApps.size());
  for (auto& s : pinnedSlots) s.isPinned = true;
  runningSlots.resize(runningSnap.groups.size());
  {
    std::lock_guard<std::mutex> lock(app.trayMutex);
    traySlots.resize(app.trayItems.size());
  }

  Slot settingsSlot, spotlightSlot, appMenuSlot, appDrawerSlot;
  settingsSlot.kind = Slot::Kind::Settings;
  spotlightSlot.kind = Slot::Kind::Spotlight;
  appMenuSlot.kind = Slot::Kind::AppMenu;
  appDrawerSlot.kind = Slot::Kind::AppDrawer;
  auto widget_blocked = [&](const std::string& w) -> bool {
    if (app.settings.widgetsEnabled) return false;
    if (w == "pinned_apps" || w == "running_apps") return false;
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
      return eh::shell::dock_slot_hooks::dock_media_slot_width(nullptr, sc, s.key, icon, barH, mprisSnap);
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
    return icon;
  };

  auto append_widget = [&](std::vector<Slot>& out, const std::string& wid) {
    if (!eh::config::widget_instance_enabled(sc, wid)) return;
    if (widget_blocked(wid)) return;
    if (wid == "pinned_apps") { for (const auto& s : pinnedSlots) out.push_back(s); }
    else if (wid == "running_apps") { for (const auto& s : runningSlots) out.push_back(s); }
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
                               const eh::mpris::PlayerSnapshot& mprisSnap) {
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

  struct Slot {
    using Kind = eh::widgets::shared_slot_paint::SlotKind;
    Kind kind = Kind::App;
    std::string key;
    std::string iconId;
    std::uint64_t chosenSerial = 0;
    bool anyActivated = false;
    bool isPinned = false;
  };

  // Running groups come from the per-draw snapshot built in taskbar_draw().
  const std::vector<eh::shell::shared::RunningGroup>& runningGroups = runningSnap.groups;
  const uint64_t settingsChosenSerial = runningSnap.settingsChosenSerial;
  const bool settingsActivated = runningSnap.settingsActivated;

  // Build tray snapshot
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

  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();

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
    if (w == "pinned_apps" || w == "running_apps") return false;
    if (eh::config::widget_token_is_system_tray(w)) return false;
    if (w == "settings_button" || w == "distro_spotlight" || w == "app_menu" ||
        w == "trash") return false;
    const std::string impl = eh::config::widget_implementation_type(w);
    return impl == "clock" || impl == "world_clock" || impl == "weather" || impl == "media" || impl == "workspaces" ||
           impl == "control_center" || impl == "notifications" || impl == "volume_mixer" ||
           impl == "app_drawer";
  };

  auto append_widget = [&](std::vector<Slot>& out, const std::string& wid) {
    if (!eh::config::widget_instance_enabled(sc, wid)) return;
    if (widget_blocked(wid)) return;
    if (wid == "pinned_apps") {
      for (const auto& s : pinnedSlots) out.push_back(s);
    } else if (wid == "running_apps") {
      for (const auto& s : runningSlots) out.push_back(s);
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

  const auto& scAct = eh::config::shell_config_snapshot();
  const double globalScale = std::clamp(scAct.dock.shellUiScale, 0.5, 2.0);
  const double taskbarUIScale = std::clamp(app.settings.scale, 0.5, 2.0) * globalScale;
  const double iconRaw = static_cast<double>(app.settings.iconSize) * taskbarUIScale;
  // Clamp icon so it always fits within the bar with at least 4px breathing room
  // on each side, matching how the dock constrains bar height >= icon + 4.
  const double icon = std::clamp(iconRaw, 8.0, std::max(8.0, boxH - 8.0));
  const double gap = static_cast<double>(app.settings.iconSpacing) * taskbarUIScale;

  std::unordered_map<const void*, double> slotWidthCache;
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
      return eh::shell::dock_slot_hooks::dock_media_slot_width(nullptr, sc, s.key, icon, boxH, mprisSnap);
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
    return icon;
  };
  auto paint_slot_w = [&](const Slot& s) -> double {
    const void* key = static_cast<const void*>(&s);
    auto cit = slotWidthCache.find(key);
    if (cit != slotWidthCache.end()) return cit->second;
    const double w = paint_slot_w_uncached(s);
    slotWidthCache.emplace(key, w);
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

  auto gap_after_local = [&](size_t idx) -> double {
    if (idx + 1 >= all.size()) return 0.0;
    return gap_between(all[idx], all[idx + 1]);
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
  const double stripPad = eh::shell::taskbar::strip_pad_px(taskbarUIScale);
  const double secGapPaint = eh::shell::taskbar::section_gap_px(taskbarUIScale);
  const double stripInner = stripPad * 2.0;
  const auto tW0 = std::chrono::steady_clock::now();
  const double lw = section_width(left);
  const double cw = section_width(center);
  const double rw = section_width(right);
  const auto lr = tb_lr_strip_layout(x, boxW, stripInner, lw, cw, rw, secGapPaint);
  const bool use_lr = center.empty() && lr.side_by_side;

  double totalW = 0.0;
  if (use_lr) {
    totalW = lw + secGapPaint + rw;
  } else {
    for (size_t i = 0; i < all.size(); i++) {
      totalW += paint_slot_w(all[i]);
      if (i + 1 < all.size()) totalW += gap_after_local(i);
    }
  }
  app.sectionMs[0] += 0.0;
  {
    const double wms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tW0).count();
    app.sectionMs[1] += 0.0;
    app.sectionMs[2] += 0.0;
    (void)wms;
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
        // Cover the whole slot: pinned/running app slots can be wider than
        // their icon when a label is shown, and an icon-sized rect only lit
        // up half the item.
        rounded_rect(ix - 2.0, iconY + liftY - 2.0, slotW + 4.0, icon + 4.0, 14.0);
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
        if (eh::shell::dock_slot_hooks::paint_media_slot(cr, sc, s.key, ix, iconY + liftY, slotW, icon, icon, mprisSnap, hovered, pressed, mediaHoverBtn, app.pointerX, app.pointerY))
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
        // Pill outline behind consecutive pinned apps
        if (s.isPinned && app.settings.pinnedAppsTrayPill) {
          const bool firstPinnedInRun = (i == 0 || slots[i - 1].kind != Slot::Kind::App || !slots[i - 1].isPinned);
          if (firstPinnedInRun) {
            size_t runEnd = i;
            while (runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::App && slots[runEnd + 1].isPinned)
              runEnd++;
            const bool appendRightTrash = runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::Trash;
            double pillX = ix;
            double runW = static_cast<double>(runEnd - i + 1) * icon;
            if (appendRightTrash) {
              const double g = gap_local(runEnd);
              runW += g + icon;
            }
            eh::widgets::slot_pill_style::paint_pill(cr, pillX, iconY, runW, icon);
          }
        }
        // Pill outline behind consecutive running apps
        if (!s.isPinned && app.settings.runningAppsTrayPill) {
          const bool firstRunningInRun = (i == 0 || slots[i - 1].kind != Slot::Kind::App || slots[i - 1].isPinned);
          if (firstRunningInRun) {
            size_t runEnd = i;
            while (runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::App && !slots[runEnd + 1].isPinned)
              runEnd++;
            const bool appendRightTrash = runEnd + 1 < slots.size() && slots[runEnd + 1].kind == Slot::Kind::Trash;
            double pillX = ix;
            double runW = static_cast<double>(runEnd - i + 1) * icon;
            if (appendRightTrash) {
              const double g = gap_local(runEnd);
              runW += g + icon;
            }
            eh::widgets::slot_pill_style::paint_pill(cr, pillX, iconY, runW, icon);
          }
        }
        if (hovered && !s.isPinned) {
          const double cellRad = eh::shell::dock_slot_hooks::slot_pill::corner_radius(icon, icon);
          rounded_rect(ix, iconY + liftY, icon, icon, cellRad);
          eh::shell::dock_slot_hooks::slot_pill::set_fill_for_state(cr, true, false);
          cairo_fill(cr);
        }
        const std::string lookup = !s.iconId.empty() ? s.iconId : s.key;
        if (const auto* ic = paint_app_icon(app.icons, lookup)) {
          if (ic->surface) {
            const double pad2 = 6.0;
            const double avail = icon - pad2 * 2.0;
            const double sx = avail / std::max(1, ic->width);
            const double sy = avail / std::max(1, ic->height);
            const double sc_i = std::min(sx, sy);
            const double dw = ic->width * sc_i;
            const double dh = ic->height * sc_i;
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
      if (s.kind == Slot::Kind::App && app.toplevels && toplevel_by_serial(*app.toplevels, s.chosenSerial)) {
        cairo_arc(cr, ix + icon / 2.0, dotCy, dotR, 0, 2 * M_PI);
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
    render_section(center, panelCenterX, static_cast<int>(left.size()));
    app.sectionMs[1] += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tC0).count();
    const auto tR0 = std::chrono::steady_clock::now();
    render_section(right, panelRightX, static_cast<int>(left.size() + center.size()));
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
        render_section(all, startX, 0);
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
        render_section(all, startX, 0);
      }
    }
  }

  // Floating pin rendering for drag-to-reorder
  if (app.pinDragging && !floatingPinLookup.empty()) {
    const double fx = std::max(x + 4.0, std::min(x + boxW - icon - 4.0, app.pointerX - icon / 2.0));
    const double fy = iconY - 4.0;
    rounded_rect(fx - 2.0, fy - 2.0, icon + 4.0, icon + 4.0, 14.0);
    cairo_set_source_rgba(cr, 1.00, 1.00, 1.00, 0.12);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.55, 0.80, 1.00, 0.65);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    if (const auto* ic = paint_app_icon(app.icons, floatingPinLookup)) {
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
    if (floatingPinRunning) {
      const double fBottom = fy + icon;
      const double fDotR = 3.0;
      const double fDotCy = fBottom + kRunIndicatorGapPx + fDotR;
      cairo_arc(cr, fx + icon / 2.0, fDotCy, fDotR, 0, 2 * M_PI);
      if (floatingPinActivated) cairo_set_source_rgba(cr, 0.4, 0.7, 1.0, 0.95);
      else cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
      cairo_fill(cr);
    }
  }
}

}
