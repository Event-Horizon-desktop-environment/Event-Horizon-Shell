#include "backends/sway/sway_backends.h"

#include <nlohmann/json.hpp>

SwayKeyboardSource::SwayKeyboardSource(wspace::sway::SwaySession& runtime) : m_state(runtime) {   }

bool SwayKeyboardSource::ready() const noexcept {
   
  return m_state.canConnect();
}

bool SwayKeyboardSource::nextLayout() const {
   
  if (!ready()) return false;
  return m_state.sendCommand(wspace::sway::SwaySession::kRunCommand,
                           "input type:keyboard xkb_switch_layout next")
      .has_value();
}

std::optional<LayoutSet> SwayKeyboardSource::currentLayout() const {
   
  if (!ready()) return std::nullopt;

  const auto response = m_state.sendCommand(wspace::sway::SwaySession::kGetInputs, {});
  if (!response.has_value() || !response->is_array()) return std::nullopt;

  for (const auto& input : *response) {
    if (!input.is_object()) continue;
    if (input.value("type", "") != "keyboard") continue;

    const auto layout = input.value("xkb_active_layout_name", std::string{});
    if (!layout.empty()) {
      return LayoutSet{.activeIndex = 0, .names = {layout}};
    }
  }

  return std::nullopt;
}

std::optional<std::string> SwayKeyboardSource::activeLayoutName() const {
   
  const auto state = currentLayout();
  if (!state.has_value() || state->names.empty()) return std::nullopt;
  return state->names[0];
}
