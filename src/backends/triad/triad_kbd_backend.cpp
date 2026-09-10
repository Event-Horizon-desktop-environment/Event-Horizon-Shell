#include "backends/triad/triad_backends.h"

#include <nlohmann/json.hpp>

static nlohmann::json const* extractTriadState(nlohmann::json const& envelope) {
  if (!envelope.is_object()) return nullptr;
  auto triad = envelope.find("triad");
  if (triad == envelope.end() || !triad->is_object()) return nullptr;
  auto state = triad->find("state");
  return (state != triad->end() && state->is_object()) ? &*state : nullptr;
}

static std::optional<LayoutInfo> decodeLayoutInfo(nlohmann::json const& envelope) {
  auto* state = extractTriadState(envelope);
  if (!state) return std::nullopt;

  auto names = state->find("keyboard_layouts");
  auto current = state->find("current_keyboard_layout_idx");
  if (names == state->end() || !names->is_array() || current == state->end() || !current->is_number_integer())
    return std::nullopt;

  LayoutInfo info;
  info.activeIndex = current->get<int>();
  info.names.reserve(names->size());
  for (auto const& entry : *names) {
    if (!entry.is_string()) return std::nullopt;
    info.names.push_back(entry.get<std::string>());
  }
  if (info.activeIndex < 0 || info.activeIndex >= static_cast<int>(info.names.size())) return std::nullopt;
  return info;
}

TriadKeyboardBackend::TriadKeyboardBackend(wspace::triad::TriadRuntime& rt) : m_core(rt) {}

bool TriadKeyboardBackend::ready() const noexcept { return m_core.canConnect(); }

bool TriadKeyboardBackend::nextLayout() const {
  return ready() && m_core.dispatchAction("switch-keyboard-layout", {{"layout", "next"}});
}

std::optional<LayoutInfo> TriadKeyboardBackend::currentLayout() const {
  if (!ready()) return std::nullopt;
  auto response = m_core.sendQuery("state");
  return response ? decodeLayoutInfo(*response) : std::nullopt;
}

std::optional<std::string> TriadKeyboardBackend::activeLayoutName() const {
  auto info = currentLayout();
  if (!info || info->activeIndex < 0 || info->activeIndex >= static_cast<int>(info->names.size()))
    return std::nullopt;
  return info->names[static_cast<std::size_t>(info->activeIndex)];
}
