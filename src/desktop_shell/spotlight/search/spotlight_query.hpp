#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "desktop_shell/spotlight/search/spotlight_search.hpp"
#include "desktop_shell/spotlight/providers/seeker_registry.hpp"

struct DockApp;

void eh_spotlight_apps_query(std::string_view filter, std::vector<SpotlightHit>* out, int max_rows,
                               const std::vector<eh::shell::seeker::Window>& windows = {});

namespace eh::shell::dock {

void dock_spotlight_refresh_results(DockApp& app);

}
