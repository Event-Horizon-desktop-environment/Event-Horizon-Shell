#pragma once

#include "desktop_shell/common/icon_cache/icon_cache.hpp"

#include <string>

namespace eh::shell::mixer_icon {

struct StreamIconIds {
  std::string icon_name;
  std::string app_id;
  std::string process_binary;
  std::string process_path;
  std::string app_name;
};

[[nodiscard]] const eh::icons::IconEntry* resolve_mixer_stream_theme_icon(eh::icons::IconCache& icons,
                                                                          const StreamIconIds& s);

// Winning-key variant: records which single lookup reproduces the result as
// "a:<key>" / "t:<key>" (empty when nothing resolved), so paint can memoize
// one cheap call per stream instead of the full ~20-attempt cascade.
[[nodiscard]] const eh::icons::IconEntry* resolve_mixer_stream_theme_icon(eh::icons::IconCache& icons,
                                                                          const StreamIconIds& s,
                                                                          std::string* outWinningKey);

}
