#pragma once

#include "backends/contract/keyboard.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace eh::wayland {
class WaylandLink;
}

namespace wspace::mango {

class MangoSession {
public:
  MangoSession() = default;

  void refresh();

  [[nodiscard]] bool runCommand(std::string_view cmd) const;
  [[nodiscard]] std::optional<nlohmann::json> ipcQuery(std::string_view cmd) const;
  [[nodiscard]] const std::string& socketPath() const;
  [[nodiscard]] bool available() const;

private:
  void resolveIfPending() const;
  void locateEndpoint() const;

  mutable bool m_resolved = false;
  mutable std::string m_endpoint;
};

bool mangoSetOutputPower(eh::wayland::WaylandLink& wl, bool on);

} // namespace wspace::mango

class MangoKeyboardSource {
private:
  wspace::mango::MangoSession& m_state;

public:
  explicit MangoKeyboardSource(wspace::mango::MangoSession& ref);
  [[nodiscard]] std::optional<std::string> activeLayoutName() const;
  [[nodiscard]] std::optional<LayoutSet> currentLayout() const;
  [[nodiscard]] bool nextLayout() const;
  [[nodiscard]] bool ready() const noexcept;
};

class MangoOutputSource {
private:
  wspace::mango::MangoSession& m_rt;

public:
  explicit MangoOutputSource(wspace::mango::MangoSession& ref);
  [[nodiscard]] std::optional<std::string> focusedOutputName() const;
};
