#include "desktop_shell/dock/pinned/dock_pinned.h"

#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/dock/pinned/dock_pin_identity.hpp"
#include "desktop_shell/shared/pins/pin_identity.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>

namespace {

// Returns the id that should be persisted for a new pin. When the requested id
// resolves to a real .desktop file we store the canonical stem (e.g.
// "com.blackmagicdesign.resolve", "sh.cider.Cider") instead of the running
// window's short WM_CLASS appId ("resolve", "cider"), which changes depending
// on the compositor and cannot be used to look the entry back up. Only apps
// without a .desktop entry (wine, custom binaries) fall back to the running
// toplevel appId so the pin still matches the live window.
std::string dock_pin_store_id(DockApp& app, const std::string& key, const std::string& appKey) {
  if (auto desktop = find_desktop_file_for_appid(key)) {
    const std::string& p = *desktop;
    const auto slash = p.rfind('/');
    const std::string base = (slash == std::string::npos) ? p : p.substr(slash + 1);
    if (base.size() > 8 && base.ends_with(".desktop")) return base.substr(0, base.size() - 8);
    if (!base.empty()) return base;
  }
  const std::vector<std::string> lookupKeys = pin_identity_keys_for_raw(appKey);
  for (const auto& tl : app.toplevels) {
    if (!tl.handle || tl.closed) continue;
    const std::string tn = eh::shell::paths::normalize_desktop_app_id(tl.appId);
    bool hit = (tn == key);
    if (!hit) {
      for (const auto& c : lookupKeys) {
        if (c == tn) {
          hit = true;
          break;
        }
      }
    }
    if (hit) return tl.appId;
  }
  return key;
}

// Settings-only variant of dock_pin_store_id: resolves a .desktop file to its
// canonical stem but skips the running-toplevel fallback (no DockApp here), so
// apps without a .desktop entry store their raw app key.
std::string dock_settings_store_id(const std::string& key) {
  if (auto desktop = find_desktop_file_for_appid(key)) {
    const std::string& p = *desktop;
    const auto slash = p.rfind('/');
    const std::string base = (slash == std::string::npos) ? p : p.substr(slash + 1);
    if (base.size() > 8 && base.ends_with(".desktop")) return base.substr(0, base.size() - 8);
    if (!base.empty()) return base;
  }
  return key;
}

}  // namespace

bool dock_is_app_pinned(DockApp& app, const std::string& rawDesktopOrAppKey) {
   
  const std::string key = eh::shell::paths::normalize_desktop_app_id(rawDesktopOrAppKey);
  if (key == "unknown" || key == eh::shell::kSettingsAppId || key == eh::shell::kSlotKeySettings || key == eh::shell::kSlotKeySep) return false;
  for (const auto& p : app.settings.pinnedApps) {
    if (pin_identity_pin_raw_matches_key(p, key)) return true;
  }
  return false;
}

bool dock_settings_is_app_pinned(const std::string& rawDesktopOrAppKey) {
  const std::string key = eh::shell::paths::normalize_desktop_app_id(rawDesktopOrAppKey);
  if (key == "unknown" || key == eh::shell::kSettingsAppId || key == eh::shell::kSlotKeySettings || key == eh::shell::kSlotKeySep) return false;
  for (const auto& p : eh::config::shell_config_snapshot().dock.pinnedApps) {
    if (pin_identity_pin_raw_matches_key(p, key)) return true;
  }
  return false;
}

void dock_settings_pinned_toggle(const std::string& appKey) {
  auto sc = eh::config::shell_config_snapshot();
  const std::string key = eh::shell::paths::normalize_desktop_app_id(appKey);
  if (key == "unknown" || key == eh::shell::kSettingsAppId || key == eh::shell::kSlotKeySettings || key == eh::shell::kSlotKeySep) return;

  auto& pins = sc.dock.pinnedApps;
  for (auto it = pins.begin(); it != pins.end(); ++it) {
    if (!pin_identity_pin_raw_matches_key(*it, key)) continue;
    std::cout << "[dock] unpin (settings-only): '" << key << "'\n";
    pins.erase(it);
    (void)dock_save_dock_settings(sc.dock);
    return;
  }

  std::cout << "[dock] pin (settings-only): '" << key << "'\n";
  pins.push_back(dock_settings_store_id(key));
  (void)dock_save_dock_settings(sc.dock);
}

void dock_pinned_toggle(DockApp& app, const std::string& appKey) {
   
  const std::string key = eh::shell::paths::normalize_desktop_app_id(appKey);
  if (key == "unknown" || key == eh::shell::kSettingsAppId || key == eh::shell::kSlotKeySettings || key == eh::shell::kSlotKeySep) return;

  auto& pins = app.settings.pinnedApps;
  dock_pin_identity_cache_refresh(app);
  for (auto it = pins.begin(); it != pins.end(); ++it) {
    if (!pin_identity_pin_raw_matches_key(*it, key)) continue;
    std::cout << "[dock] unpin: '" << key << "'\n";
    pins.erase(it);
    (void)dock_save_dock_settings(app.settings);
    app.settingsMtime = eh::config::aggregate_config_source_mtime();
    app.sizeDirty = true;
    app.dockPinIdentityCacheFingerprint.clear();
    dock_draw(app);
    return;
  }

  std::cout << "[dock] pin: '" << key << "'\n";
  pins.push_back(dock_pin_store_id(app, key, appKey));
  (void)dock_save_dock_settings(app.settings);
  app.settingsMtime = eh::config::aggregate_config_source_mtime();
  app.sizeDirty = true;
  app.dockPinIdentityCacheFingerprint.clear();
  dock_draw(app);
}

void dock_pinned_toggle_silent(DockApp& app, const std::string& appKey) {
  const std::string key = eh::shell::paths::normalize_desktop_app_id(appKey);
  if (key == "unknown" || key == eh::shell::kSettingsAppId || key == eh::shell::kSlotKeySettings || key == eh::shell::kSlotKeySep) return;

  auto& pins = app.settings.pinnedApps;
  dock_pin_identity_cache_refresh(app);
  for (auto it = pins.begin(); it != pins.end(); ++it) {
    if (!pin_identity_pin_raw_matches_key(*it, key)) continue;
    std::cout << "[dock] unpin (silent): '" << key << "'\n";
    pins.erase(it);
    (void)dock_save_dock_settings(app.settings);
    app.settingsMtime = eh::config::aggregate_config_source_mtime();
    app.sizeDirty = true;
    app.dockPinIdentityCacheFingerprint.clear();
    return;
  }

  std::cout << "[dock] pin (silent): '" << key << "'\n";
  pins.push_back(dock_pin_store_id(app, key, appKey));
  (void)dock_save_dock_settings(app.settings);
  app.settingsMtime = eh::config::aggregate_config_source_mtime();
  app.sizeDirty = true;
  app.dockPinIdentityCacheFingerprint.clear();
}

bool dock_is_start_menu_pinned(DockApp& app, const std::string& rawDesktopOrAppKey) {
  const std::string key = eh::shell::paths::normalize_desktop_app_id(rawDesktopOrAppKey);
  if (key == "unknown" || key == eh::shell::kSettingsAppId || key == eh::shell::kSlotKeySettings || key == eh::shell::kSlotKeySep) return false;
  for (const auto& p : app.settings.startMenuPinnedApps) {
    if (pin_identity_pin_raw_matches_key(p, key)) return true;
  }
  return false;
}

void dock_start_menu_pinned_toggle(DockApp& app, const std::string& appKey) {
  const std::string key = eh::shell::paths::normalize_desktop_app_id(appKey);
  if (key == "unknown" || key == eh::shell::kSettingsAppId || key == eh::shell::kSlotKeySettings || key == eh::shell::kSlotKeySep) return;

  auto& pins = app.settings.startMenuPinnedApps;
  constexpr size_t kMaxStartMenuPins = 128;
  for (auto it = pins.begin(); it != pins.end(); ++it) {
    if (!pin_identity_pin_raw_matches_key(*it, key)) continue;
    pins.erase(it);
    (void)dock_save_dock_settings(app.settings);
    app.settingsMtime = eh::config::aggregate_config_source_mtime();
    eh_app_drawer_refresh_hits(app);
    return;
  }

  if (pins.size() >= kMaxStartMenuPins) return;
  pins.push_back(dock_pin_store_id(app, key, appKey));
  (void)dock_save_dock_settings(app.settings);
  app.settingsMtime = eh::config::aggregate_config_source_mtime();
  eh_app_drawer_refresh_hits(app);
}

bool dock_settings_drawer_pinned(const DockSettings& s, const std::string& rawDesktopOrAppKey) {
  const std::string key = eh::shell::paths::normalize_desktop_app_id(rawDesktopOrAppKey);
  if (key == "unknown" || key == eh::shell::kSettingsAppId || key == eh::shell::kSlotKeySettings || key == eh::shell::kSlotKeySep) return false;
  for (const auto& p : s.drawerPinnedApps) {
    if (pin_identity_pin_raw_matches_key(p, key)) return true;
  }
  return false;
}

bool dock_is_drawer_pinned(DockApp& app, const std::string& rawDesktopOrAppKey) {
  return dock_settings_drawer_pinned(app.settings, rawDesktopOrAppKey);
}

void dock_drawer_pinned_toggle(DockApp& app, const std::string& appKey) {
  const std::string key = eh::shell::paths::normalize_desktop_app_id(appKey);
  if (key == "unknown" || key == eh::shell::kSettingsAppId || key == eh::shell::kSlotKeySettings || key == eh::shell::kSlotKeySep) return;

  auto& pins = app.settings.drawerPinnedApps;
  constexpr size_t kMaxDrawerPins = 15;
  for (auto it = pins.begin(); it != pins.end(); ++it) {
    if (!pin_identity_pin_raw_matches_key(*it, key)) continue;
    pins.erase(it);
    (void)dock_save_dock_settings(app.settings);
    app.settingsMtime = eh::config::aggregate_config_source_mtime();
    return;
  }

  if (pins.size() >= kMaxDrawerPins) return;
  pins.push_back(dock_pin_store_id(app, key, appKey));
  (void)dock_save_dock_settings(app.settings);
  app.settingsMtime = eh::config::aggregate_config_source_mtime();
}

void dock_pinned_pointer_motion(DockApp& app) {

  if (!app.pinDragCandidate && !app.pinDragging) return;
  const double dx = app.pointerX - app.pinDragStartX;
  const double dy = app.pointerY - app.pinDragStartY;
  const double dist2 = dx * dx + dy * dy;
  if (app.pinDragCandidate && !app.pinDragging) {
    if (dist2 < (6.0 * 6.0)) return;
    app.pinDragging = true;
    app.pinDragLayerOnlyNextDraw = false;
    if (eh_dock_pin_drag_perf_enabled()) {
      std::cerr << "[dock-pin-perf] threshold_cross key=" << app.pinDragKey << " ptr=(" << app.pointerX << ","
                << app.pointerY << ") dist2=" << dist2 << "\n";
    }
    if (!app.frameCallback) dock_schedule_frame(app);
    else if (eh_dock_pin_drag_perf_enabled()) ++app.pinDragPerfScheduleNoop;
  }
  if (!app.pinDragging) return;
  if (app.pinDragKey.empty()) return;

  const bool perf = eh_dock_pin_drag_perf_enabled();
  const auto perf_t0 = perf ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

  const DockPinnedDragGeometry geo = dock_pinned_drag_geometry(app);
  if (!geo.valid || geo.pinnedCount < 2) return;

  const std::vector<std::string>& pinSrc = dock_pinned_apps_source_for_layout(app);
  std::vector<std::string> pinnedKeys;
  pinnedKeys.reserve(pinSrc.size());
  for (const auto& pRaw : pinSrc) {
    const std::string p = eh::shell::paths::normalize_desktop_app_id(pRaw);
    if (p == "unknown" || p == eh::shell::kSettingsAppId) continue;
    pinnedKeys.push_back(p);
  }
  if (pinnedKeys.empty()) return;

  const int pinsCount = static_cast<int>(pinnedKeys.size());
  if (pinsCount != geo.pinnedCount) return;

  const double pinnedRight =
      geo.firstPinnedLeftSurf + static_cast<double>(geo.pinnedCount) * geo.slotStrideSurf - (geo.slotStrideSurf - geo.iconSurf);
  const double pxHit = std::clamp(app.pointerX, geo.firstPinnedLeftSurf, pinnedRight);

  int curIdx = -1;
  for (int i = 0; i < pinsCount; i++) {
    if (pinnedKeys[static_cast<size_t>(i)] == app.pinDragKey) {
      curIdx = i;
      break;
    }
  }
  if (curIdx < 0) return;
  if (pinsCount <= 1) return;

  const int reducedCount = pinsCount - 1;
  int reducedSlot = 0;
  std::vector<double> centers;
  centers.reserve(static_cast<size_t>(reducedCount));
  for (int i = 0; i < pinsCount; i++) {
    if (i == curIdx) continue;
    const double cx = geo.firstPinnedLeftSurf + static_cast<double>(reducedSlot) * geo.slotStrideSurf + geo.iconSurf * 0.5;
    centers.push_back(cx);
    ++reducedSlot;
  }

  int rawInsert = reducedCount;
  for (int i = 0; i < reducedCount; i++) {
    const double slotLeft = geo.firstPinnedLeftSurf + static_cast<double>(i) * geo.slotStrideSurf;
    const double slotMid = slotLeft + geo.iconSurf * 0.5;
    if (pxHit < slotMid) {
      rawInsert = i;
      break;
    }
  }
  rawInsert = std::clamp(rawInsert, 0, reducedCount);

  int insertInReduced = rawInsert;
  const int committed = app.pinDragInsertIdx;
  if (committed >= 0 && rawInsert != committed) {
    const double hyst = std::clamp(geo.iconSurf * 0.12, 4.0, 12.0);
    bool accept = false;
    if (rawInsert > committed) {
      if (committed < reducedCount) {
        const double mid =
            geo.firstPinnedLeftSurf + static_cast<double>(committed) * geo.slotStrideSurf + geo.iconSurf * 0.5;
        if (pxHit >= mid + hyst) accept = true;
      }
    } else {
      if (committed > 0) {
        const double mid =
            geo.firstPinnedLeftSurf + static_cast<double>(committed - 1) * geo.slotStrideSurf + geo.iconSurf * 0.5;
        if (pxHit <= mid - hyst) accept = true;
      }
    }
    if (!accept) insertInReduced = committed;
  }

  if (insertInReduced == app.pinDragInsertIdx) {
    ++app.pinDragPerfGhostCoalesce;
    app.pinDragLayerOnlyNextDraw = true;
    if (!app.frameCallback) dock_schedule_frame(app);
    else if (perf) ++app.pinDragPerfScheduleNoop;
    return;
  }
  app.pinDragInsertIdx = insertInReduced;
  app.pinDragLayerOnlyNextDraw = false;
  ++app.pinDragPerfInsertChanges;

  dock_pin_drag_rebuild_paint_order(app);
  app.pinDragDirty = (app.pinDragPaintOrder != app.pinDragPinsSnapshot);

  const bool had_frame_before = app.frameCallback != nullptr;
  if (!app.frameCallback) dock_schedule_frame(app);
  else if (perf) ++app.pinDragPerfScheduleNoop;

  if (perf) {
    const auto perf_t1 = std::chrono::steady_clock::now();
    const double motion_ms = std::chrono::duration<double, std::milli>(perf_t1 - perf_t0).count();

    const uint32_t n = app.pinDragPerfInsertChanges;
    if (n == 1u || (n % 10u) == 0u) {
      std::cerr << "[dock-pin-perf] insert_change reduced_insert=" << insertInReduced << " pins=" << pinsCount
                << " motion_ms=" << motion_ms << " px_hit=" << pxHit << " had_frame_before_schedule="
                << (had_frame_before ? 1 : 0) << " insert_seq=" << n << (n > 1u && (n % 10u) == 0u ? " (sampled)" : "")
                << "\n";
    }
  }
}

const std::vector<std::string>& dock_pinned_apps_source_for_layout(const DockApp& app) {
  if (app.pinDragging && !app.pinDragPinsSnapshot.empty()) {
    if (!app.pinDragPaintOrder.empty()) return app.pinDragPaintOrder;
    return app.pinDragPinsSnapshot;
  }
  return app.settings.pinnedApps;
}

void dock_pin_drag_rebuild_paint_order(DockApp& app) {
  if (!app.pinDragging || app.pinDragPinsSnapshot.empty()) {
    app.pinDragPaintOrder.clear();
    return;
  }
  if (app.pinDragInsertIdx < 0) {
    app.pinDragPaintOrder = app.pinDragPinsSnapshot;
    return;
  }
  if (app.pinDragKey.empty()) {
    app.pinDragPaintOrder.clear();
    return;
  }

  std::vector<std::string> pins = app.pinDragPinsSnapshot;
  std::vector<int> rawIndices;
  rawIndices.reserve(pins.size());
  for (int i = 0; i < static_cast<int>(pins.size()); i++) {
    const std::string k = eh::shell::paths::normalize_desktop_app_id(pins[static_cast<size_t>(i)]);
    if (k == "unknown" || k == eh::shell::kSettingsAppId) continue;
    rawIndices.push_back(i);
  }

  std::vector<std::string> pinStrings;
  pinStrings.reserve(rawIndices.size());
  for (int ri : rawIndices) pinStrings.push_back(pins[static_cast<size_t>(ri)]);

  int curIdx = -1;
  for (int i = 0; i < static_cast<int>(pinStrings.size()); ++i) {
    if (eh::shell::paths::normalize_desktop_app_id(pinStrings[static_cast<size_t>(i)]) == app.pinDragKey) {
      curIdx = i;
      break;
    }
  }
  if (curIdx < 0 || static_cast<int>(pinStrings.size()) <= 1) {
    app.pinDragPaintOrder = std::move(pins);
    return;
  }

  const int reducedCount = static_cast<int>(pinStrings.size()) - 1;
  const int insertAt = std::clamp(app.pinDragInsertIdx, 0, reducedCount);

  const std::string moved = pinStrings[static_cast<size_t>(curIdx)];
  pinStrings.erase(pinStrings.begin() + curIdx);
  pinStrings.insert(pinStrings.begin() + insertAt, moved);

  for (size_t i = 0; i < pinStrings.size(); ++i)
    pins[static_cast<size_t>(rawIndices[i])] = std::move(pinStrings[i]);
  app.pinDragPaintOrder = std::move(pins);
}
