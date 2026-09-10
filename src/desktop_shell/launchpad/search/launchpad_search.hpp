#pragma once

#include "desktop_shell/dock/core/dock_settings.hpp"

#include <string>
#include <string_view>
#include <vector>

struct LaunchpadHit {
  std::string path;
  std::string name;
  std::string exec;
  std::string iconKey;
  // If non-empty, this entry represents a folder.
  // folderAppIds holds the normalized desktop file IDs of the children.
  std::vector<std::string> folderAppIds{};
  // Index into launchpad folders array, -1 means not a folder.
  int folder_def_idx = -1;
};

namespace eh::shell::launchpad {

void launchpad_search_run(std::string_view query_utf8, const std::vector<std::string>* pins_ordered,
                          std::vector<LaunchpadHit>* out);

// Optimized re-search: re-scores only entries that matched a previous query.
// prev_query must be non-empty and the new query must extend it by at least one
// character.  previous_cat_indices holds catalog indices from the last full search.
void launchpad_search_filter(std::string_view query_utf8,
                             const std::vector<size_t>& previous_cat_indices,
                             const std::vector<std::string>* pins_ordered,
                             std::vector<LaunchpadHit>* out);
}
