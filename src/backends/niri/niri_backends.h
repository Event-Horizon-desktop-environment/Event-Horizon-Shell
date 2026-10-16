#pragma once

#include "backends/contract/keyboard.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

class NiriSession {
public:
  NiriSession() = default;

  [[nodiscard]] bool canConnect() const;
  [[nodiscard]] const std::string& queryEndpoint() const;
  [[nodiscard]] std::optional<nlohmann::json> sendQuery(std::string_view req) const;
  [[nodiscard]] bool sendCheck(std::string_view req, bool allowNoResponse = false) const;
  [[nodiscard]] bool dispatchAction(const nlohmann::json& action, bool allowNoResponse = false) const;
  void rescan();

private:
  struct ReplyInfo;
  [[nodiscard]] ReplyInfo transmit(std::string_view req) const;
  void resolveIfPending() const;
  void locateEndpoint() const;

  mutable bool m_resolved = false;
  mutable std::string m_endpoint;
};

bool niriSetOutputPower(NiriSession& state, bool on);

class NiriKeyboardSource {
private:
  NiriSession& m_state;

public:
  explicit NiriKeyboardSource(NiriSession& ref);
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool nextLayout() const;
  [[nodiscard]] std::optional<LayoutSet> currentLayout() const;
  [[nodiscard]] std::optional<std::string> activeLayoutName() const;
};

class NiriOutputSource {
private:
  NiriSession& m_state;

public:
  explicit NiriOutputSource(NiriSession& ref);
  [[nodiscard]] std::optional<std::string> focusedOutputName() const;
};
