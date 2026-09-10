#pragma once

#include "backends/interfaces/workspace_manager.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct wl_output;
struct ext_workspace_manager_v1;

class CompositorRuntimeRegistry;
class ExtWorkspaceManager;
class HyprlandWorkspaceManager;
class MangoWorkspaceManager;
class SwayWorkspaceManager;
class TriadWorkspaceManager;

class WaylandWorkspaces {
public:
  using ChangeCallback = std::function<void()>;

  explicit WaylandWorkspaces(CompositorRuntimeRegistry& runtimeRegistry);
  ~WaylandWorkspaces();

  void bindExtProtocol(ext_workspace_manager_v1* manager);
  void setOutputResolver(std::function<std::string(wl_output*)> resolver);
  void initialize();
  void outputAttached(wl_output* output);
  void outputDetached(wl_output* output);
  void onStateChange(ChangeCallback callback);
  void jumpTo(const std::string& id);
  void jumpToOnOutput(wl_output* output, const std::string& id);
  void jumpToOnOutput(wl_output* output, const DeskRegion& workspace);
  void teardown();
  [[nodiscard]] int eventFd() const noexcept;
  [[nodiscard]] short eventFlags() const noexcept;
  [[nodiscard]] int eventTimeout() const noexcept;
  void onEvent(short revents);
  [[nodiscard]] const char* name() const noexcept;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>> appsByDesk(wl_output* output) const;
  [[nodiscard]] TaskbarMode taskbarMode() const noexcept;
  [[nodiscard]] std::unordered_map<std::uintptr_t, DeskWindow>
  matchEntries(const std::vector<TaskbarEntry>& windows, wl_output* output) const;
  [[nodiscard]] std::vector<DeskWindow> windowsOnDesk(wl_output* output) const;
  void bringToFront(const std::string& windowId) const;

  [[nodiscard]] std::vector<DeskRegion> allRegions() const;
  [[nodiscard]] std::vector<DeskRegion> regionsOnOutput(wl_output* output) const;

  [[nodiscard]] wl_output* mangoIpcSelectedOutput() const;
  [[nodiscard]] std::optional<std::pair<std::string, std::string>> mangoIpcFocusedClientOnOutput(wl_output* output) const;
  [[nodiscard]] ISocketConnector* mangoIpcConnector() const noexcept;
  [[nodiscard]] std::optional<std::string> focusedWindowId() const;

  [[nodiscard]] HyprlandWorkspaceManager* hyprlandBackend() const noexcept;
  [[nodiscard]] IWorkspaceManager* workspace_backend() const noexcept { return m_activeBackend; }

private:
  void setActiveBackend(IWorkspaceManager* backend);
  void notifyChanged() const;

  std::vector<std::unique_ptr<IWorkspaceManager>> m_backends;
  std::vector<IOutputWatcher*> m_outputObservers;
  std::vector<IOutputNameLookup*> m_outputNameResolvers;
  IExtProtocolHandler* m_extWorkspaceBinder = nullptr;
  IWorkspaceManager* m_extBackend = nullptr;
  IWorkspaceManager* m_mangoIpcBackend = nullptr;
  IWorkspaceManager* m_hyprlandBackend = nullptr;
  IWorkspaceManager* m_swayBackend = nullptr;
  IWorkspaceManager* m_triadBackend = nullptr;
  ISocketConnector* m_mangoIpcConnector = nullptr;
  ISocketConnector* m_hyprlandConnector = nullptr;
  ISocketConnector* m_swayConnector = nullptr;
  IWorkspaceManager* m_activeBackend = nullptr;
  ChangeCallback m_changeCallback;
};

void set_global_wayland_workspaces(WaylandWorkspaces* ww);
WaylandWorkspaces* global_wayland_workspaces();
