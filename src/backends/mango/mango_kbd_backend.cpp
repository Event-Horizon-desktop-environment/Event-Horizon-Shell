#include "backends/mango/mango_backends.h"

MangoKeyboardSource::MangoKeyboardSource(wspace::mango::MangoSession& rt) : m_state(rt) {}

std::optional<std::string> MangoKeyboardSource::activeLayoutName() const {
  auto resp = m_state.ipcQuery("get keyboardlayout");
  if (resp.has_value() && resp->is_object()) {
    auto it = resp->find("layout");
    if (it != resp->end() && it->is_string()) {
      return it->get<std::string>();
    }
  }
  return std::nullopt;
}

std::optional<LayoutSet> MangoKeyboardSource::currentLayout() const {
  auto name = activeLayoutName();
  if (!name.has_value()) { return std::nullopt; }
  LayoutSet st;
  st.activeIndex = 0;
  st.names.push_back(*name);
  return st;
}

bool MangoKeyboardSource::nextLayout() const {
  return m_state.runCommand("switch_keyboard_layout");
}

bool MangoKeyboardSource::ready() const noexcept { return m_state.available(); }
