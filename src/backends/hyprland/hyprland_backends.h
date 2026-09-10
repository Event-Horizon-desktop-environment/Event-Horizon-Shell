#pragma once

#include "backends/interfaces/kbd_interface.h"

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wspace::hyprland {

class HyprlandRuntime;
class HyprlandEventHandler;

struct SocketPaths {
  std::string req;
  std::string ev;
};

class HyprlandRuntime {
public:
  HyprlandRuntime();
  ~HyprlandRuntime();

  HyprlandRuntime(const HyprlandRuntime&) = delete;
  HyprlandRuntime& operator=(const HyprlandRuntime&) = delete;

  [[nodiscard]] bool canConnect() const noexcept { return m_fd >= 0; }
  [[nodiscard]] bool startSession();
  [[nodiscard]] bool isLuaConfig() const noexcept { return m_luaMode; }

  [[nodiscard]] std::optional<std::string> sendCommand(std::string_view command) const;
  [[nodiscard]] std::optional<nlohmann::json> sendQuery(std::string_view command) const;

  void rescan();
  void shutdown();

  void registerEventHandler(HyprlandEventHandler* handler);
  void unregisterEventHandler(HyprlandEventHandler* handler);

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
  std::vector<HyprlandEventHandler*> m_listeners;
};

[[nodiscard]] bool hyprland_config_is_lua();
std::string fmtAddress(std::uint64_t addr);
std::optional<std::uint64_t> parseHexAddress(std::string_view in);
std::string canonicalWindowId(std::string_view raw);
bool idMatch(std::string_view a, std::string_view b);
bool setOutputPower(HyprlandRuntime& state, bool on);

class HyprlandEventHandler {
public:
  explicit HyprlandEventHandler(HyprlandRuntime& rt);
  HyprlandEventHandler(HyprlandEventHandler const&) = delete;
  HyprlandEventHandler& operator=(HyprlandEventHandler const&) = delete;
  virtual ~HyprlandEventHandler();

  virtual void onEvent(std::string_view ev, std::string_view data) = 0;
  virtual void onCleanup() {}
  virtual void onStateChange() {}

protected:
  HyprlandRuntime& m_rt;
};

} // namespace wspace::hyprland

bool hyprland_ipc_send(std::string_view command);
std::optional<std::string> hyprland_ipc_request(std::string_view command);

class HyprlandKeyboardBackend {
private:
  wspace::hyprland::HyprlandRuntime& m_state;

public:
  explicit HyprlandKeyboardBackend(wspace::hyprland::HyprlandRuntime& ref);
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool nextLayout() const;
  [[nodiscard]] std::optional<LayoutInfo> currentLayout() const;
  [[nodiscard]] std::optional<std::string> activeLayoutName() const;
};

class HyprlandOutputBackend {
private:
  wspace::hyprland::HyprlandRuntime& m_state;

public:
  explicit HyprlandOutputBackend(wspace::hyprland::HyprlandRuntime& ref);
  [[nodiscard]] std::optional<std::string> focusedOutputName() const;
};
