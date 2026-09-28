#include "desktop_shell/spotlight/search/spotlight_query.hpp"

#include <algorithm>

#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/popup/geometry/layout.hpp"
#include "desktop_shell/spotlight/search/spotlight_search.hpp"
#include "desktop_shell/spotlight/providers/seeker_registry.hpp"

void eh_spotlight_apps_query(std::string_view query, std::vector<SpotlightHit>* out, int maxResults,
                               const std::vector<eh::shell::seeker::Window>& windows) {
   
  std::vector<SpotlightHit> hits;
  eh_app_drawer_menu_query(query, nullptr, &hits);
  eh::shell::seeker::query_providers(query, hits, windows);
  std::partial_sort(hits.begin(), hits.begin() + std::min(static_cast<size_t>(maxResults), hits.size()), hits.end(),
    [](const SpotlightHit& a, const SpotlightHit& b) { return a.score > b.score; });
  hits.resize(std::min(static_cast<size_t>(maxResults), hits.size()));
  *out = std::move(hits);
}

namespace eh::shell::dock {

void dock_spotlight_refresh_results(DockApp& app) {
   
  std::vector<eh::shell::seeker::Window> wins;
  for (const auto& tl : app.toplevels.list()) {
    if (!tl.handle) continue;
    eh::shell::seeker::Window w;
    w.appId = tl.appId;
    w.title = tl.title;
    w.serial = tl.serial;
    wins.push_back(std::move(w));
  }
  eh_spotlight_apps_query(app.spotlightQuery, &app.spotlightHits, eh::shell::dock::kSpotlightMaxRows(), wins);
  if (app.spotlightHits.empty()) {
    app.spotlightSel = -1;
  } else if (app.spotlightSel < 0 || app.spotlightSel >= static_cast<int>(app.spotlightHits.size())) {
    app.spotlightSel = 0;
  }
}

}
