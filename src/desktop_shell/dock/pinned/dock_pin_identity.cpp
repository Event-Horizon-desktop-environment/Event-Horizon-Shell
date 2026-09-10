#include "desktop_shell/dock/pinned/dock_pin_identity.hpp"

#include "desktop_shell/shared/pins/pin_identity.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using eh::shell::paths::normalize_desktop_app_id;

void dock_pin_identity_cache_refresh(DockApp& app) {
   
  std::vector<std::string> sortedP = app.settings.pinnedApps;
  std::sort(sortedP.begin(), sortedP.end());
  std::string fp;
  fp.reserve(sortedP.size() * 12u + 4u);
  for (const auto& p : sortedP) {
    fp.push_back('\0');
    fp += p;
  }
  if (fp == app.dockPinIdentityCacheFingerprint) return;
  if (eh_dock_pin_drag_perf_enabled()) {
    std::cerr << "[dock-pin-perf] pin_identity_cache REBUILD distinct_pins=" << sortedP.size()
              << " (desktop/WMC reads per pin)\n";
  }
  app.dockPinIdentityCacheFingerprint = std::move(fp);
  app.dockPinIdentityKeys.clear();
  for (const auto& pRaw : app.settings.pinnedApps) {
    const std::string pn = normalize_desktop_app_id(pRaw);
    if (pn == "unknown" || pn == eh::shell::kSettingsAppId) continue;
    app.dockPinIdentityKeys[pn] = pin_identity_keys_for_raw(pRaw);
  }
}

bool dock_pin_identity_list_contains_toplevel_key(DockApp& app, const std::string& normalizedToplevelAppId) {
   
  dock_pin_identity_cache_refresh(app);
  for (const auto& pRaw : app.settings.pinnedApps) {
    const std::string pn = normalize_desktop_app_id(pRaw);
    if (pn == "unknown" || pn == eh::shell::kSettingsAppId) continue;
    auto it = app.dockPinIdentityKeys.find(pn);
    if (it != app.dockPinIdentityKeys.end()) {
      for (const auto& k : it->second) {
        if (k == normalizedToplevelAppId) return true;
      }
    }
    if (pin_identity_same_resolved_desktop(pn, normalizedToplevelAppId)) return true;
  }
  return false;
}

bool dock_pin_identity_norm_matches_anchor(DockApp& app, const std::string& anchorNormalizedAppKey,
                                           const std::string& normalizedToplevelAppId) {
   
  dock_pin_identity_cache_refresh(app);
  if (anchorNormalizedAppKey == normalizedToplevelAppId) return true;
  if (auto it = app.dockPinIdentityKeys.find(anchorNormalizedAppKey); it != app.dockPinIdentityKeys.end()) {
    for (const auto& k : it->second) {
      if (k == normalizedToplevelAppId) return true;
    }
  }
  return pin_identity_same_resolved_desktop(anchorNormalizedAppKey, normalizedToplevelAppId);
}
