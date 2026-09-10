#pragma once

#include "desktop_shell/desktop/entries/desktop_entry_types.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

[[nodiscard]] bool is_generic_runtime_binary(const std::string& lowercased);

std::optional<DesktopEntryInfo> read_desktop_entry_info(const std::string& desktopFilePath);

std::optional<std::string> find_desktop_file_for_appid(const std::string& appId);

[[nodiscard]] const eh::icons::IconEntry* mixer_icon_from_pinned_apps(eh::icons::IconCache& icons,
                                                                       const std::vector<std::string>& pinnedApps,
                                                                       const std::string& process_binary);

struct StreamDesktopIds {
  std::string process_binary;
  std::string app_id;
  std::string app_name;
  std::string node_name;
  std::string node_description;
};

[[nodiscard]] std::optional<DesktopEntryInfo> resolve_desktop_entry_for_stream(const StreamDesktopIds& ids);
