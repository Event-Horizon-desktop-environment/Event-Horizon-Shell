#pragma once

#include "backends/hyprland/hyprland_backends.h"
#include "backends/interfaces/workspace_manager.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class HyprlandWorkspaceManager final
    : public IWorkspaceManager,
      public IOutputNameLookup,
      public ISocketConnector,
      public wspace::hyprland::HyprlandEventHandler {
public:
  struct WorkspaceState {
    int id = -1;
    std::string name;
    std::string monitor;
    bool active = false;
    bool urgent = false;
    bool occupied = false;
  };

  struct ToplevelState {
    int workspaceId;
    std::string appId;
    std::string title;
    bool urgent = false;
  };

  explicit HyprlandWorkspaceManager(wspace::hyprland::HyprlandRuntime& runtime);

  [[nodiscard]] const char* name() const override { return "hyprland-ipc"; }
  [[nodiscard]] bool ready() const noexcept override;
  bool openConnection() override;
  void onStateChange(IWorkspaceManager::Notify cb) override;
  void jumpTo(const std::string& id) override;
  void jumpToOnOutput(wl_output* output, const std::string& id) override;
  void jumpToOnOutput(wl_output* output, const DeskRegion& ws) override;
  [[nodiscard]] std::vector<DeskRegion> allRegions() const override;
  [[nodiscard]] std::vector<DeskRegion> regionsOnOutput(wl_output* output) const override;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
  appsByDesk(wl_output* output) const override;
  void teardown() override;

  void setOutputResolver(IOutputNameLookup::Resolver r) override;

  [[nodiscard]] int eventFd() const noexcept { return m_backend.pollHandle(); }
  void onEvent(short revents) { m_backend.handlePoll(revents); }

  void onEvent(std::string_view event, std::string_view data) override;
  void onCleanup() override;
  void onStateChange() override;

  [[nodiscard]] std::optional<std::string> focusedWindowId() const;
  void refreshSnapshot();

private:
  void loadWorkspaces();
  void loadMonitors();
  void loadClients();
  void refreshFlags();

  void handleFocusChange(std::string_view mon, int wsId);
  void dismissUrgent(int wsId);
  void relocateWindow(std::uint64_t addr, int wsId);

  [[nodiscard]] WorkspaceState* findWorkspace(int id);
  [[nodiscard]] static std::optional<std::uint64_t> decodeHex(std::string_view val);
  [[nodiscard]] static std::optional<int> decodeInt(std::string_view val);
  [[nodiscard]] static std::vector<std::string_view> splitByN(std::string_view data, std::size_t count);

  [[nodiscard]] int findIdForName(const std::string& id) const;

  wspace::hyprland::HyprlandRuntime& m_backend;
  std::vector<WorkspaceState> m_spaces;
  std::unordered_map<std::uint64_t, ToplevelState> m_windows;
  std::unordered_map<std::string, int> m_activeMap;
  IWorkspaceManager::Notify m_notify;
  IOutputNameLookup::Resolver m_resolver;
};
