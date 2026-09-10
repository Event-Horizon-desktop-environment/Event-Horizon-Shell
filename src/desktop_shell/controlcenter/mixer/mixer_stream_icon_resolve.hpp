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

}
