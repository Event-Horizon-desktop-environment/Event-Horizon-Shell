#pragma once

#include "backends/interfaces/kbd_interface.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace wspace::triad {

class TriadRuntime {
public:
  TriadRuntime() = default;

  [[nodiscard]] bool canConnect() const;
  [[nodiscard]] std::string const& queryEndpoint() const;
  [[nodiscard]] std::optional<nlohmann::json> sendQuery(std::string_view request) const;
  [[nodiscard]] std::optional<nlohmann::json> sendPayload(nlohmann::json body) const;
  [[nodiscard]] bool dispatchAction(std::string_view name, nlohmann::json parameters = nlohmann::json::object()) const;
  void rescan();

private:
  enum class TransferOutcome { Success, SocketMissing, TransmitFailure, ReceiveFailure, ParseError };

  struct ExchangeResult {
    TransferOutcome outcome = TransferOutcome::SocketMissing;
    std::optional<nlohmann::json> data;
  };

  [[nodiscard]] ExchangeResult performExchange(std::string_view message) const;
  void resolveOnDemand() const;
  void lookupSocket() const;

  mutable bool m_initialized = false;
  mutable std::string m_endpoint;
};

bool setMonitorPower(TriadRuntime& backend, bool enable);

} // namespace wspace::triad

class TriadKeyboardBackend {
private:
  wspace::triad::TriadRuntime& m_core;

public:
  explicit TriadKeyboardBackend(wspace::triad::TriadRuntime& ref);
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool nextLayout() const;
  [[nodiscard]] std::optional<LayoutInfo> currentLayout() const;
  [[nodiscard]] std::optional<std::string> activeLayoutName() const;
};

class TriadOutputBackend {
private:
  wspace::triad::TriadRuntime& m_backend;

public:
  explicit TriadOutputBackend(wspace::triad::TriadRuntime& ref);
  [[nodiscard]] std::optional<std::string> focusedOutputName() const;
};
