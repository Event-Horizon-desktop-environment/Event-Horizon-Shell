#pragma once

#include <string>
#include <string_view>

namespace eh::shell::dock::tray {

[[nodiscard]] bool dock_tray_item_hidden_by_env(std::string_view id, std::string_view title, std::string_view service,
                                                std::string_view path);
}

namespace eh::tray {
inline bool tray_item_hidden_by_env(std::string_view id, std::string_view title, std::string_view service,
                                    std::string_view path) {
   
  return eh::shell::dock::tray::dock_tray_item_hidden_by_env(id, title, service, path);
}
}
