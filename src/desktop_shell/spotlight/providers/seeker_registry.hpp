#pragma once

#include "desktop_shell/spotlight/search/spotlight_search.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace eh::shell::seeker {

struct Provider {
  std::string name;
  std::string prefix;
  bool global = false;
  std::function<void(std::string_view query, std::vector<SpotlightHit>& out)> query;
};

struct Window {
  std::string appId;
  std::string title;
  std::uint64_t serial = 0;
};

void register_provider(Provider p);
void query_providers(std::string_view text, std::vector<SpotlightHit>& out,
                     const std::vector<Window>& windows = {});
const std::vector<Window>& staged_window_list();

}
