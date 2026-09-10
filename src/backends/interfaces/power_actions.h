#pragma once

#include "backends/interfaces/kbd_interface.h"
#include "backends/interfaces/workspace_manager.h"
#include "desktop_shell/unified/compositor_kind.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <poll.h>
#include <string>
#include <unordered_map>
#include <vector>

struct wl_output;
struct ext_workspace_manager_v1;
struct zdwl_ipc_manager_v2;
class CompositorRuntimeRegistry;
class WaylandConnection;
class WaylandWorkspaces;

class HyprlandKeyboardBackend;
class HyprlandOutputBackend;
class NiriKeyboardBackend;
class NiriOutputBackend;
class SwayKeyboardBackend;
class SwayOutputBackend;
class MangoKeyboardBackend;
class MangoOutputBackend;
class TriadKeyboardBackend;
class TriadOutputBackend;

struct WorkspaceWindowAssignment {
  std::string windowId;
  std::string workspaceKey;
  std::string appId;
  std::string title;
  std::int32_t x = 0;
  std::int32_t y = 0;
};

namespace eh::wayland {
class WaylandConnection;
}

class CompositorPlatform {
public:
  using ChangeCallback = std::function<void()>;

  explicit CompositorPlatform(eh::wayland::WaylandConnection& wl);
  ~CompositorPlatform();

  CompositorPlatform(const CompositorPlatform&) = delete;
  CompositorPlatform& operator=(const CompositorPlatform&) = delete;

  void initialize();
  void cleanup();

  [[nodiscard]] CompositorKind compositor_kind() const { return kind_; }
  [[nodiscard]] const char* compositor_name() const { return compositor_kind_cstr(kind_); }

  void setWorkspaceChangeCallback(ChangeCallback callback);
  void onOverviewChange(ChangeCallback callback);
  void activateWorkspace(const std::string& id);
  void activateWorkspace(wl_output* output, const std::string& id);
  void activateWorkspace(wl_output* output, const DeskRegion& workspace);
  std::size_t addWorkspacePollFds(std::vector<pollfd>& fds) const;
  [[nodiscard]] int workspacePollTimeoutMs() const noexcept;
  void dispatchWorkspacePoll(const std::vector<pollfd>& fds, std::size_t startIdx);
  [[nodiscard]] std::vector<DeskRegion> workspaces(wl_output* output = nullptr) const;
  [[nodiscard]] std::unordered_map<std::string, std::vector<std::string>>
  appsByDesk(wl_output* outputFilter = nullptr) const;
  [[nodiscard]] std::vector<std::string> workspaceDisplayKeys(wl_output* outputFilter = nullptr) const;
  [[nodiscard]] std::vector<WorkspaceWindowAssignment>
  workspaceWindowAssignments(wl_output* outputFilter = nullptr) const;
  [[nodiscard]] TaskbarMode taskbarMode() const noexcept;
  [[nodiscard]] std::unordered_map<std::uintptr_t, DeskWindow>
  matchEntries(const std::vector<TaskbarEntry>& windows, wl_output* outputFilter = nullptr) const;
  [[nodiscard]] const char* workspaceBackendName() const noexcept;

  [[nodiscard]] bool tracksOverviewState() const noexcept;
  [[nodiscard]] bool hasOverview() const noexcept;
  [[nodiscard]] bool overviewActive() const noexcept;

  [[nodiscard]] IWorkspaceManager* workspace_backend() const;
  [[nodiscard]] WaylandWorkspaces& wayland_workspaces() const;

  [[nodiscard]] std::optional<std::string> focused_output_name() const;
  bool set_output_power(bool on);

  // Tells the compositor to quit when the session is shutting down.
  void requestSessionExit();
  // The output new windows and interactions should be placed on.
  [[nodiscard]] wl_output* preferredInteractiveOutput() const;
  // The focused client (title, app-id) on the preferred output.
  [[nodiscard]] std::optional<std::pair<std::string, std::string>> activeToplevel() const;

  [[nodiscard]] bool has_keyboard_backend() const { return !!cycle_layout_; }
  [[nodiscard]] bool cycle_keyboard_layout() const;
  [[nodiscard]] std::optional<LayoutInfo> keyboard_layout_state() const;
  [[nodiscard]] std::optional<std::string> current_keyboard_layout_name() const;

private:
  struct WorkspaceModelSnapshot {
    std::uint32_t outputName = 0;
    std::vector<DeskRegion> workspaces;
    std::vector<WorkspaceWindowAssignment> assignments;
  };

  void create_backends();
  [[nodiscard]] std::string connectorNameForOutput(wl_output* output) const;
  [[nodiscard]] std::vector<WorkspaceModelSnapshot> workspaceModelSnapshot() const;
  [[nodiscard]] static bool sameWorkspaceModelSnapshot(
      const std::vector<WorkspaceModelSnapshot>& lhs, const std::vector<WorkspaceModelSnapshot>& rhs);

  template <typename T, typename B>
  void wire_keyboard(std::unique_ptr<T>& storage, B& runtime);
  template <typename T, typename B>
  void wire_output(std::unique_ptr<T>& storage, B& runtime, bool (*set_power)(B&, bool));

  CompositorKind kind_ = CompositorKind::Unknown;
  eh::wayland::WaylandConnection& wl_;

  // Backends live here for their whole lifetime; the wiring lambdas hold raw pointers.
  std::unique_ptr<NiriKeyboardBackend> niri_kb_;
  std::unique_ptr<NiriOutputBackend> niri_out_;
  std::unique_ptr<HyprlandKeyboardBackend> hyprland_kb_;
  std::unique_ptr<HyprlandOutputBackend> hyprland_out_;
  std::unique_ptr<SwayKeyboardBackend> sway_kb_;
  std::unique_ptr<SwayOutputBackend> sway_out_;
  std::unique_ptr<MangoKeyboardBackend> mango_kb_;
  std::unique_ptr<MangoOutputBackend> mango_out_;
  std::unique_ptr<TriadKeyboardBackend> triad_kb_;
  std::unique_ptr<TriadOutputBackend> triad_out_;

  std::function<bool()> cycle_layout_;
  std::function<std::optional<LayoutInfo>()> layout_state_;
  std::function<std::optional<std::string>()> current_layout_name_;
  std::function<std::optional<std::string>()> focused_output_;
  std::function<bool(bool)> set_output_power_;

  std::unique_ptr<wspace::IWorkspaceDataBackend> m_workspaceMetadataBackend;
  std::optional<bool> m_lastRequestedOutputPowerState;

  ChangeCallback m_workspaceChangeCallback;
  ChangeCallback m_overviewChangeCallback;
  std::vector<WorkspaceModelSnapshot> m_lastWorkspaceModelSnapshot;
};
