#pragma once

#include "backends/contract/keyboard.h"

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wspace::hyprland {

class HyprlandSession;
class HyprlandEventSink;

struct SocketPaths {
  std::string req;
  std::string ev;
};

class HyprlandSession {
public:
  HyprlandSession();
  ~HyprlandSession();

  HyprlandSession(const HyprlandSession&) = delete;
  HyprlandSession& operator=(const HyprlandSession&) = delete;

  [[nodiscard]] bool canConnect() const noexcept { return m_fd >= 0; }
  [[nodiscard]] bool startSession();
  [[nodiscard]] bool isLuaConfig() const noexcept { return m_luaMode; }

  [[nodiscard]] std::optional<std::string> sendCommand(std::string_view command) const;
  [[nodiscard]] std::optional<nlohmann::json> sendQuery(std::string_view command) const;

  void rescan();
  void shutdown();

  void addEventSink(HyprlandEventSink* handler);
  void removeEventSink(HyprlandEventSink* handler);

  [[nodiscard]] int pollHandle() const noexcept { return m_fd; }
  void handlePoll(short revents);

private:
  void resolveIfPending() const;
  void locateEndpoints() const;
  void readConfig();
  void drainSocket();
  void processLines();
  void routeEvent(std::string_view line);

  int m_fd = -1;
  std::vector<char> m_buffer;
  mutable bool m_pathsKnown = false;
  bool m_luaMode = false;
  mutable SocketPaths m_sockets;
  std::vector<HyprlandEventSink*> m_listeners;
};

[[nodiscard]] bool hyprland_config_is_lua();
std::string fmtAddress(std::uint64_t addr);
std::optional<std::uint64_t> parseHexAddress(std::string_view in);
std::string canonicalWindowId(std::string_view raw);
bool idMatch(std::string_view a, std::string_view b);
bool setOutputPower(HyprlandSession& state, bool on);

class HyprlandEventSink {
public:
  explicit HyprlandEventSink(HyprlandSession& rt);
  HyprlandEventSink(HyprlandEventSink const&) = delete;
  HyprlandEventSink& operator=(HyprlandEventSink const&) = delete;
  virtual ~HyprlandEventSink();

  virtual void onEvent(std::string_view ev, std::string_view data) = 0;
  virtual void onCleanup() {}
  virtual void onStateChange() {}

protected:
  HyprlandSession& m_rt;
};

} // namespace wspace::hyprland

bool hyprland_ipc_send(std::string_view command);
std::optional<std::string> hyprland_ipc_request(std::string_view command);

class HyprlandKeyboardSource {
private:
  wspace::hyprland::HyprlandSession& m_state;

public:
  explicit HyprlandKeyboardSource(wspace::hyprland::HyprlandSession& ref);
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool nextLayout() const;
  [[nodiscard]] std::optional<LayoutSet> currentLayout() const;
  [[nodiscard]] std::optional<std::string> activeLayoutName() const;
};

class HyprlandOutputSource {
private:
  wspace::hyprland::HyprlandSession& m_state;

public:
  explicit HyprlandOutputSource(wspace::hyprland::HyprlandSession& ref);
  [[nodiscard]] std::optional<std::string> focusedOutputName() const;
};
