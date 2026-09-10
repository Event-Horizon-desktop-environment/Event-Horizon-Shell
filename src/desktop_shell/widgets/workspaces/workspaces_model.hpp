#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eh::widgets {

struct WorkspaceEntry {
  int id = 0;
  std::string label;
  bool active = false;
  bool occupied = false;
  bool urgent = false;

  std::vector<std::string> app_keys{};
};

enum class WorkspaceDisplayMode : std::uint8_t { Id, Name, None };

}
