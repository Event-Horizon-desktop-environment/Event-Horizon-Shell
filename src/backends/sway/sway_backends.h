#pragma once

#include "backends/contract/keyboard.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wspace::sway {

class SwaySession {
public:
  static constexpr std::string_view kIpcMagic = "i3-ipc";

  SwaySession() = default;

  [[nodiscard]] bool canConnect() const;
  [[nodiscard]] const std::string& queryEndpoint() const;
  [[nodiscard]] std::optional<nlohmann::json> sendCommand(std::uint32_t type, std::string_view payload) const;
  void rescan();

  static constexpr std::uint32_t kRunCommand = 0;
  static constexpr std::uint32_t kGetWorkspaces = 1;
  static constexpr std::uint32_t kSubscribe = 2;
  static constexpr std::uint32_t kGetOutputs = 3;
  static constexpr std::uint32_t kGetTree = 4;
  static constexpr std::uint32_t kGetInputs = 100;
  static constexpr std::uint32_t kWorkspaceEvent = 0x80000000u;
  static constexpr std::uint32_t kWindowEvent = 0x80000003u;

private:
  struct Frame { std::uint32_t type = 0; std::string payload; };
  Frame transmit(std::uint32_t type, std::string_view payload) const;
  void resolveIfPending() const;
  void locateEndpoint() const;

  mutable bool m_resolved = false;
  mutable std::string m_endpoint;
};

bool swaySetOutputPower(SwaySession& ctx, bool on);

} // namespace wspace::sway

class SwayKeyboardSource {
private:
  wspace::sway::SwaySession& m_state;

public:
  explicit SwayKeyboardSource(wspace::sway::SwaySession& ref);
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool nextLayout() const;
  [[nodiscard]] std::optional<LayoutSet> currentLayout() const;
  [[nodiscard]] std::optional<std::string> activeLayoutName() const;
};

class SwayOutputSource {
private:
  wspace::sway::SwaySession& m_ctx;

public:
  explicit SwayOutputSource(wspace::sway::SwaySession& ref);
  [[nodiscard]] std::optional<std::string> focusedOutputName() const;
};
