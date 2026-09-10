#pragma once

#include <string>
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
