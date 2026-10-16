#pragma once

#include "backends/contract/desk_catalog.h"
#include "backends/contract/desk_source.h"
#include "backends/contract/desk_types.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

struct wl_output;
struct ext_workspace_manager_v1;

#include <utility>
#include <vector>

#include "backends/contract/output_link.h"
#include "backends/contract/protocol_binders.h"

class CompositorSessionRegistry;
class ExtWorkspaceManager;
class HyprlandWorkspaceManager;

class CompositorWorkspaces {
public:
  using DeskChangeCallback = std::function<void()>;

  // Desk key -> app ids present on it.
  using DeskAppIndex = std::unordered_map<std::string, std::vector<std::string>>;
  // Row handle -> window on that row.
  using RowWindowIndex = std::unordered_map<std::uintptr_t, ToplevelRef>;
  // App id + window title of the focused toplevel.
  using FocusedTitles = std::optional<std::pair<std::string, std::string>>;

  explicit CompositorWorkspaces(CompositorSessionRegistry& runtimeRegistry);
  ~CompositorWorkspaces();

  [[nodiscard]] const char* name() const noexcept;

  // --- protocol binding ------------------------------------------------
  void bindExtProtocol(ext_workspace_manager_v1* manager);
  void setOutputResolver(std::function<std::string(wl_output*)> resolver);

  // --- output life cycle -----------------------------------------------
  void outputAttached(wl_output* output);
  void outputDetached(wl_output* output);

  // --- polling ----------------------------------------------------------
  [[nodiscard]] int eventTimeout() const noexcept;
  [[nodiscard]] short eventFlags() const noexcept;
  [[nodiscard]] int eventFd() const noexcept;
  void onEvent(short revents);

  // --- navigation -------------------------------------------------------
  void jumpToOnOutput(wl_output* output, const DeskSlot& workspace);
  void jumpToOnOutput(wl_output* output, const std::string& id);
  void jumpTo(const std::string& id);

  // --- desk queries -----------------------------------------------------
  [[nodiscard]] std::vector<DeskSlot> regionsOnOutput(wl_output* output) const;
  [[nodiscard]] std::vector<DeskSlot> allRegions() const;

  // --- bar / window integration ----------------------------------------
  [[nodiscard]] std::vector<ToplevelRef> windowsOnDesk(wl_output* output) const;
  [[nodiscard]] RowWindowIndex matchEntries(const std::vector<BarItem>& windows, wl_output* output) const;
  [[nodiscard]] BarMatchPolicy taskbarMode() const noexcept;
  [[nodiscard]] DeskAppIndex appsByDesk(wl_output* output) const;
  void bringToFront(const std::string& windowId) const;

  // --- focus reporting --------------------------------------------------
  [[nodiscard]] std::optional<std::string> focusedWindowTitle() const;
  [[nodiscard]] FocusedTitles mangoFocusedToplevel(wl_output* output) const;
  [[nodiscard]] wl_output* mangoChosenOutput() const;

  // --- owner accessors ---------------------------------------------------
  [[nodiscard]] ILinkOpener* mangoIpcConnector() const noexcept;
  [[nodiscard]] HyprlandWorkspaceManager* hyprlandBackend() const noexcept;
  [[nodiscard]] IDeskSource* workspace_backend() const noexcept { return m_activeBackend; }

  void initialize();
  void onStateChange(DeskChangeCallback callback);
  void teardown();

private:
  void setActiveBackend(IDeskSource* backend);
  void notifyChanged() const;

  // Runs `fn` against every non-null entry of `list`; accepts raw-pointer
  // and smart-pointer containers alike.
  template <typename List, typename Fn>
  static void callEach(List& list, Fn&& fn) {
    for (auto&& backend : list) {
      if (backend != nullptr) { fn(*backend); }
    }
  }

  // The mango IPC backend when it is built and linked, else null.
  [[nodiscard]] IDeskSource const* readyMango() const;

  std::vector<std::unique_ptr<IDeskSource>> m_backends;
  std::vector<IOutputLifecycle*> m_outputObservers;
  std::vector<IOutputResolver*> m_outputNameResolvers;
  IExtWorkspaceBinding* m_extWorkspaceBinder = nullptr;
  DeskChangeCallback m_changeCallback;
  IDeskSource* m_activeBackend = nullptr;
  ILinkOpener* m_hyprlandConnector = nullptr;
  ILinkOpener* m_swayConnector = nullptr;
  ILinkOpener* m_mangoIpcConnector = nullptr;
  IDeskSource* m_swayBackend = nullptr;
  IDeskSource* m_hyprlandBackend = nullptr;
  IDeskSource* m_mangoIpcBackend = nullptr;
  IDeskSource* m_extBackend = nullptr;
};

class MangoWorkspaceManager;
class SwayWorkspaceManager;

void set_global_wayland_workspaces(CompositorWorkspaces* ww);
CompositorWorkspaces* global_wayland_workspaces();
