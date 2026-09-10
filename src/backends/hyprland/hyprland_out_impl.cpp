#include "backends/hyprland/hyprland_backends.h"

#include <nlohmann/json.hpp>

HyprlandOutputBackend::HyprlandOutputBackend(wspace::hyprland::HyprlandRuntime& rt)
    : m_state(rt) {}

std::optional<std::string> HyprlandOutputBackend::focusedOutputName() const {
  auto json = m_state.sendQuery("j/monitors");
  if (!json.has_value() || !json->is_array()) {
    return std::nullopt;
  }
  for (auto const& mon : *json) {
    if (!mon.is_object()) { continue; }
    auto it = mon.find("focused");
    if (it != mon.end() && it->is_boolean() && it->get<bool>()) {
      auto nameIt = mon.find("name");
      if (nameIt != mon.end() && nameIt->is_string()) {
        return nameIt->get<std::string>();
      }
    }
  }
  return std::nullopt;
}

namespace wspace::hyprland {
bool setOutputPower(HyprlandRuntime& rt, bool enabled) {
  auto cmd = enabled ? std::string("keyword dpms on") : std::string("keyword dpms off");
  return rt.sendCommand(cmd).has_value();
}
} // namespace wspace::hyprland
