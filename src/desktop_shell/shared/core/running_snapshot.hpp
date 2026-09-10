#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"

namespace eh::shell::shared {

struct RunningGroup {
  std::string key;
  std::string pinMatchKey;
  std::string iconId;
  std::uint64_t chosenSerial = 0;
  bool anyActivated = false;
};

struct RunningSnapshot {
  std::vector<RunningGroup> groups;
  std::uint64_t settingsChosenSerial = 0;
  bool settingsActivated = false;
};

template<typename Toplevels, typename FirstSeenMap>
inline RunningSnapshot build_running_snapshot(
    Toplevels& toplevels,
    FirstSeenMap& appFirstSeenSerial,
    bool groupApps)
{
  RunningSnapshot out;
  toplevels.prune_closed();

  if (!groupApps) {
    std::vector<std::pair<std::uint64_t, RunningGroup>> ordered;
    ordered.reserve(static_cast<size_t>(toplevels.size()));

    for (const auto& tl : toplevels) {
      if (!tl.handle || tl.closed) continue;
      const std::string norm =
          eh::shell::paths::normalize_desktop_app_id(tl.appId.empty() ? std::string("unknown") : tl.appId);
      if (norm == eh::shell::kSettingsAppId) {
        out.settingsActivated = out.settingsActivated || tl.activated;
        if (!out.settingsChosenSerial || tl.serial > out.settingsChosenSerial)
          out.settingsChosenSerial = tl.serial;
        continue;
      }

      RunningGroup g;
      g.pinMatchKey = norm;
      g.key = std::string("__eh_tl_") + std::to_string(tl.serial);
      g.iconId = tl.appId;
      g.chosenSerial = tl.serial;
      g.anyActivated = tl.activated;
      ordered.emplace_back(tl.serial, std::move(g));
    }

    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& p : ordered) out.groups.push_back(std::move(p.second));
    return out;
  }

  for (const auto& tl : toplevels) {
    if (!tl.handle || tl.closed) continue;
    const std::string key =
        eh::shell::paths::normalize_desktop_app_id(tl.appId.empty() ? std::string("unknown") : tl.appId);
    if (key == eh::shell::kSettingsAppId) {
      out.settingsActivated = out.settingsActivated || tl.activated;
      if (!out.settingsChosenSerial || tl.serial > out.settingsChosenSerial)
        out.settingsChosenSerial = tl.serial;
      continue;
    }

    if (!appFirstSeenSerial.contains(key))
      appFirstSeenSerial.emplace(key, tl.serial);

    auto it = std::find_if(out.groups.begin(), out.groups.end(),
                           [&](const RunningGroup& g) { return g.key == key; });
    if (it == out.groups.end()) {
      out.groups.push_back(RunningGroup{.key = key,
                                        .pinMatchKey = key,
                                        .iconId = tl.appId,
                                        .chosenSerial = tl.serial,
                                        .anyActivated = tl.activated});
    } else {
      it->anyActivated = it->anyActivated || tl.activated;
      if (!it->chosenSerial || tl.serial > it->chosenSerial)
        it->chosenSerial = tl.serial;
      if (it->iconId.empty()) it->iconId = tl.appId;
    }
  }

  std::sort(out.groups.begin(), out.groups.end(),
            [](const RunningGroup& a, const RunningGroup& b) { return a.key < b.key; });
  std::stable_sort(out.groups.begin(), out.groups.end(),
                   [&](const RunningGroup& a, const RunningGroup& b) {
    const uint64_t sa = appFirstSeenSerial.contains(a.pinMatchKey)
        ? appFirstSeenSerial[a.pinMatchKey] : UINT64_MAX;
    const uint64_t sb = appFirstSeenSerial.contains(b.pinMatchKey)
        ? appFirstSeenSerial[b.pinMatchKey] : UINT64_MAX;
    if (sa != sb) return sa < sb;
    return a.key < b.key;
  });

  {
    std::unordered_set<std::string> activeKeys;
    activeKeys.reserve(out.groups.size());
    for (const auto& g : out.groups) activeKeys.insert(g.pinMatchKey);
    for (auto it = appFirstSeenSerial.begin(); it != appFirstSeenSerial.end(); ) {
      if (!activeKeys.contains(it->first))
        it = appFirstSeenSerial.erase(it);
      else
        ++it;
    }
  }

  return out;
}

}
