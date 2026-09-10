#include "backends/mango/mango_backends.h"

MangoKeyboardBackend::MangoKeyboardBackend(wspace::mango::MangoRuntime& rt) : m_state(rt) {}

bool MangoKeyboardBackend::ready() const noexcept { return m_state.available(); }

bool MangoKeyboardBackend::nextLayout() const {
  return m_state.dispatch("switch_keyboard_layout");
}

std::optional<LayoutInfo> MangoKeyboardBackend::currentLayout() const {
  auto name = activeLayoutName();
  if (!name.has_value()) { return std::nullopt; }
  LayoutInfo st;
  st.names.push_back(*name);
  st.activeIndex = 0;
  return st;
}

std::optional<std::string> MangoKeyboardBackend::activeLayoutName() const {
  auto resp = m_state.request("get keyboardlayout");
  if (!resp.has_value() || !resp->is_object()) { return std::nullopt; }
  auto it = resp->find("layout");
  if (it == resp->end() || !it->is_string()) { return std::nullopt; }
  return it->get<std::string>();
}
