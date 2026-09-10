#include "backends/niri/niri_backends.h"

#include <nlohmann/json.hpp>

namespace {

std::optional<LayoutInfo> parseLayout(nlohmann::json const& resp) {
  if (!resp.is_object()) { return std::nullopt; }

  auto ok = resp.find("Ok");
  if (ok == resp.end() || !ok->is_object()) { return std::nullopt; }

  auto kbl = ok->find("KeyboardLayouts");
  if (kbl == ok->end() || !kbl->is_object()) { return std::nullopt; }

  auto names = kbl->find("names");
  auto cur = kbl->find("current_idx");
  if (names == kbl->end() || !names->is_array() || cur == kbl->end() || !cur->is_number_integer()) {
    return std::nullopt;
  }

  LayoutInfo st;
  st.activeIndex = cur->get<int>();
  st.names.reserve(names->size());
  for (auto const& e : *names) {
    if (!e.is_string()) { return std::nullopt; }
    st.names.push_back(e.get<std::string>());
  }

  if (st.activeIndex < 0 || st.activeIndex >= static_cast<int>(st.names.size())) { return std::nullopt; }
  return st;
}

} // namespace

NiriKeyboardBackend::NiriKeyboardBackend(NiriRuntime& rt) : m_state(rt) {}

bool NiriKeyboardBackend::ready() const noexcept { return m_state.canConnect(); }

bool NiriKeyboardBackend::nextLayout() const {
  if (!ready()) { return false; }
  return m_state.dispatchAction(nlohmann::json{
    {"SwitchLayout", nlohmann::json{{"layout", "Next"}}},
  });
}

std::optional<LayoutInfo> NiriKeyboardBackend::currentLayout() const {
  if (!ready()) { return std::nullopt; }
  auto resp = m_state.sendQuery("\"KeyboardLayouts\"\n");
  return resp.has_value() ? parseLayout(*resp) : std::nullopt;
}

std::optional<std::string> NiriKeyboardBackend::activeLayoutName() const {
  auto st = currentLayout();
  if (!st.has_value() || st->activeIndex < 0 ||
      static_cast<std::size_t>(st->activeIndex) >= st->names.size()) {
    return std::nullopt;
  }
  return st->names[static_cast<std::size_t>(st->activeIndex)];
}
