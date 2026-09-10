#include "backends/hyprland/hyprland_backends.h"

#include <nlohmann/json.hpp>

HyprlandKeyboardBackend::HyprlandKeyboardBackend(wspace::hyprland::HyprlandRuntime& rt)
    : m_state(rt) {}

bool HyprlandKeyboardBackend::ready() const noexcept {
  return m_state.sendQuery("j/devices").has_value();
}

bool HyprlandKeyboardBackend::nextLayout() const {
  auto json = m_state.sendQuery("j/devices");
  if (!json.has_value() || !json->is_object()) { return false; }

  auto it = json->find("keyboards");
  if (it == json->end() || !it->is_array() || it->empty()) { return false; }

  auto const* kb = &(*it)[0];
  for (auto const& k : *it) {
    if (k.is_object() && k.value("main", false)) { kb = &k; break; }
  }
  if (!kb->is_object()) { return false; }

  auto name = kb->value("name", std::string{});
  if (name.empty()) { return false; }

  return m_state.sendCommand("switchxkblayout '" + name + "' next").has_value();
}

std::optional<LayoutInfo> HyprlandKeyboardBackend::currentLayout() const {
  auto json = m_state.sendQuery("j/devices");
  if (!json.has_value() || !json->is_object()) { return std::nullopt; }

  auto it = json->find("keyboards");
  if (it == json->end() || !it->is_array() || it->empty()) { return std::nullopt; }

  auto const* kb = &(*it)[0];
  for (auto const& k : *it) {
    if (k.is_object() && k.value("main", false)) { kb = &k; break; }
  }
  if (!kb->is_object()) { return std::nullopt; }

  LayoutInfo info;
  auto names = kb->find("keymap_names");
  if (names != kb->end() && names->is_array()) {
    for (auto const& n : *names) {
      if (n.is_string()) { info.names.push_back(n.get<std::string>()); }
    }
  }

  info.activeIndex = 0;
  auto active = kb->value("active_keymap", std::string{});
  if (!active.empty()) {
    for (std::size_t i = 0; i < info.names.size(); ++i) {
      if (info.names[i] == active) { info.activeIndex = static_cast<int>(i); break; }
    }
  }

  if (info.names.empty()) { return std::nullopt; }
  return info;
}

std::optional<std::string> HyprlandKeyboardBackend::activeLayoutName() const {
  auto state = currentLayout();
  if (!state.has_value() || state->activeIndex < 0 ||
      static_cast<std::size_t>(state->activeIndex) >= state->names.size()) {
    return std::nullopt;
  }
  return state->names[static_cast<std::size_t>(state->activeIndex)];
}
