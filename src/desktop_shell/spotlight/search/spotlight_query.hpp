#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "desktop_shell/spotlight/search/spotlight_search.hpp"

struct DockApp;

void eh_spotlight_apps_query(std::string_view filter, std::vector<SpotlightHit>* out, int max_rows);

namespace eh::shell::dock {

void dock_spotlight_refresh_results(DockApp& app);

}
