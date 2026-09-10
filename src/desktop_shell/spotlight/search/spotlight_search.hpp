#pragma once

#include <string>
#include <string_view>
#include <vector>

struct SpotlightHit {
  std::string path;
  std::string name;
  std::string genericName;

  std::string comment;
  std::string exec;
  std::string iconKey;
  std::string categories;
  int score = 0;
};

namespace eh::shell::dock::spotlight {

void eh_dock_spotlight_apps_query(std::string_view query, std::vector<SpotlightHit>* out, int maxResults = 8);

}
