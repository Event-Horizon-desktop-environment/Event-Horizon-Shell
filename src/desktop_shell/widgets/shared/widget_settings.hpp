#pragma once
#include "configuration/shell_config.hpp"
#include <string>
#include <string_view>

namespace eh::widgets {

inline std::string widget_setting(const eh::config::ShellConfig& sc, std::string_view id, std::string_view key) {
  auto it = sc.widgets.find(std::string(id));
  if (it == sc.widgets.end()) return {};
  auto jt = it->second.settings.find(std::string(key));
  if (jt == it->second.settings.end()) return {};
  return jt->second;
}

}
