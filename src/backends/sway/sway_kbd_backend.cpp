#include "backends/sway/sway_backends.h"

#include <nlohmann/json.hpp>

SwayKeyboardBackend::SwayKeyboardBackend(wspace::sway::SwayRuntime& runtime) : m_state(runtime) {   }

bool SwayKeyboardBackend::ready() const noexcept {
   
  return m_state.canConnect();
}

bool SwayKeyboardBackend::nextLayout() const {
   
  if (!ready()) return false;
  return m_state.sendCommand(wspace::sway::SwayRuntime::kRunCommand,
                           "input type:keyboard xkb_switch_layout next")
      .has_value();
}

std::optional<LayoutInfo> SwayKeyboardBackend::currentLayout() const {
   
  if (!ready()) return std::nullopt;

  const auto response = m_state.sendCommand(wspace::sway::SwayRuntime::kGetInputs, {});
  if (!response.has_value() || !response->is_array()) return std::nullopt;

  for (const auto& input : *response) {
    if (!input.is_object()) continue;
    if (input.value("type", "") != "keyboard") continue;

    const auto layout = input.value("xkb_active_layout_name", std::string{});
    if (!layout.empty()) {
      return LayoutInfo{{layout}, 0};
    }
  }

  return std::nullopt;
}

std::optional<std::string> SwayKeyboardBackend::activeLayoutName() const {
   
  const auto state = currentLayout();
  if (!state.has_value() || state->names.empty()) return std::nullopt;
  return state->names[0];
}
