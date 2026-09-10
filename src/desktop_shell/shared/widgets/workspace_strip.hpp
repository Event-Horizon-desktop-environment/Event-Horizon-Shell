#pragma once

#include <string>
#include <vector>

#include "configuration/shell_config.hpp"

struct DockApp;

namespace eh::shell::shared::widgets {

template<typename SettingsT>
inline bool has_workspaces_widget(const SettingsT& st) {
  auto has = [](const std::vector<std::string>& v) {
    for (const auto& w : v)
      if (eh::config::widget_implementation_type(w) == "workspaces") return true;
    return false;
  };
  return has(st.leftWidgets) || has(st.centerWidgets) || has(st.rightWidgets);
}

}

namespace eh::shell::dock {

void dock_ensure_workspace_strip(DockApp& app);

}
