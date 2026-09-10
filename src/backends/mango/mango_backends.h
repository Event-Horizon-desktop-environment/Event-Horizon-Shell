#pragma once

#include "backends/interfaces/kbd_interface.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace eh::wayland {
class WaylandConnection;
}

namespace wspace::mango {

class MangoRuntime {
public:
  MangoRuntime() = default;

  [[nodiscard]] bool available() const;
  [[nodiscard]] const std::string& socketPath() const;
  [[nodiscard]] std::optional<nlohmann::json> request(std::string_view cmd) const;
  [[nodiscard]] bool dispatch(std::string_view cmd) const;
  void refresh();

private:
  void resolveIfPending() const;
  void locateEndpoint() const;

  mutable bool m_resolved = false;
  mutable std::string m_endpoint;
};

bool mangoSetOutputPower(eh::wayland::WaylandConnection& wl, bool on);

} // namespace wspace::mango

class MangoKeyboardBackend {
private:
  wspace::mango::MangoRuntime& m_state;

public:
  explicit MangoKeyboardBackend(wspace::mango::MangoRuntime& ref);
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool nextLayout() const;
  [[nodiscard]] std::optional<LayoutInfo> currentLayout() const;
  [[nodiscard]] std::optional<std::string> activeLayoutName() const;
};

class MangoOutputBackend {
private:
  wspace::mango::MangoRuntime& m_rt;

public:
  explicit MangoOutputBackend(wspace::mango::MangoRuntime& ref);
  [[nodiscard]] std::optional<std::string> focusedOutputName() const;
};
