#include "backends/niri/niri_backends.h"

#include <nlohmann/json.hpp>

namespace {

[[nodiscard]] bool collectNames(nlohmann::json const& names, LayoutSet& st) {
  st.names.reserve(names.size());
  for (auto const& e : names) {
    if (!e.is_string()) { return false; }
    st.names.push_back(e.get<std::string>());
  }
  return true;
}

// The switch request niri accepts for "next layout".
nlohmann::json nextLayoutRequest() {
  nlohmann::json chosen;
  chosen["layout"] = "Next";
  nlohmann::json request;
  request["SwitchLayout"] = std::move(chosen);
  return request;
}

std::optional<LayoutSet> parseLayout(nlohmann::json const& resp) {
  if (!resp.is_object()) { return std::nullopt; }

  auto ok = resp.find("Ok");
  if (ok == resp.end()) { return std::nullopt; }
  if (!ok->is_object()) { return std::nullopt; }

  auto kbl = ok->find("KeyboardLayouts");
  if (kbl == ok->end()) { return std::nullopt; }
  if (!kbl->is_object()) { return std::nullopt; }

  auto names = kbl->find("names");
  auto cur = kbl->find("current_idx");
  if (names == kbl->end()) { return std::nullopt; }
  if (!names->is_array()) { return std::nullopt; }
  if (cur == kbl->end()) { return std::nullopt; }
  if (!cur->is_number_integer()) { return std::nullopt; }

  LayoutSet st;
  st.activeIndex = cur->get<int>();
  if (!collectNames(*names, st)) { return std::nullopt; }

  auto count = static_cast<int>(st.names.size());
  if (st.activeIndex < 0 || st.activeIndex >= count) { return std::nullopt; }
  return st;
}

} // namespace

NiriKeyboardSource::NiriKeyboardSource(NiriSession& rt) : m_state(rt) {}

std::optional<std::string> NiriKeyboardSource::activeLayoutName() const {
  auto st = currentLayout();
  if (!st.has_value()) { return std::nullopt; }
  auto idx = st->activeIndex;
  if (idx < 0) { return std::nullopt; }
  if (static_cast<std::size_t>(idx) >= st->names.size()) { return std::nullopt; }
  return st->names[static_cast<std::size_t>(idx)];
}

std::optional<LayoutSet> NiriKeyboardSource::currentLayout() const {
  auto resp = ready() ? m_state.sendQuery("\"KeyboardLayouts\"\n") : std::nullopt;
  if (!resp) { return std::nullopt; }
  return parseLayout(*resp);
}

bool NiriKeyboardSource::nextLayout() const {
  if (!ready()) { return false; }
  return m_state.dispatchAction(nextLayoutRequest());
}

bool NiriKeyboardSource::ready() const noexcept { return m_state.canConnect(); }
