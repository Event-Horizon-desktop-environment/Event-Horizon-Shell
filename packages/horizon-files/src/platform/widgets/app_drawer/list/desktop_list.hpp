#pragma once

#include "platform/spotlight/search/spotlight_search.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

std::string eh_app_drawer_desktop_stem_from_path(std::string_view desktop_file_path);

void eh_app_drawer_menu_query(std::string_view filter, const std::vector<std::string>* startMenuPins,
                              std::vector<SpotlightHit>* out);

inline void eh_app_drawer_menu_query(std::string_view filter, std::vector<SpotlightHit>* out) {
  eh_app_drawer_menu_query(filter, nullptr, out);
}

void eh_app_drawer_menu_query_filtered(std::string_view filter, const std::vector<size_t>& previous_cat_indices,
                                       const std::vector<std::string>* startMenuPins, std::vector<SpotlightHit>* out);

namespace eh::shell::dock::app_drawer {

struct DesktopEntry {
  std::string path;
  std::string name;
  std::string genericName;
  std::string comment;
  std::string keywords;
  std::string categories;

  std::string mimeTypesLower;
  std::string exec;
  std::string icon;
  bool noDisplay = false;
  bool hidden = false;
  bool terminal = false;
};

const std::vector<DesktopEntry>& get_cached_entries();
void invalidate_desktop_entries_cache();

std::vector<DesktopEntry> copy_desktop_entries();

int entry_score(std::string_view pattern, const DesktopEntry& e);

bool is_standard_category(const std::string& cat);
}

namespace eh::app_drawer {
using namespace eh::shell::dock::app_drawer;
}

std::optional<eh::shell::dock::app_drawer::DesktopEntry> dock_app_drawer_entry_for_pin_in_catalog(
    const std::string& pinRaw, const std::vector<eh::shell::dock::app_drawer::DesktopEntry>& catalog);

inline std::optional<eh::shell::dock::app_drawer::DesktopEntry> eh_app_drawer_desktop_entry_for_pin_in_catalog(
    const std::string& pinRaw, const std::vector<eh::shell::dock::app_drawer::DesktopEntry>& catalog) {
  return dock_app_drawer_entry_for_pin_in_catalog(pinRaw, catalog);
}
