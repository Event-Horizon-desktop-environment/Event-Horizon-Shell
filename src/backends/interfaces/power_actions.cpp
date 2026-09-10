#include "backends/interfaces/power_actions.h"
#include "backends/interfaces/compositor_ipc.h"
#include "backends/hyprland/hyprland_backends.h"
#include "backends/niri/niri_backends.h"
#include "backends/niri/niri_workspace_manager.h"
#include "backends/mango/mango_backends.h"
#include "backends/sway/sway_backends.h"
#include "backends/triad/triad_backends.h"

#include "desktop_shell/unified/compositor_kind.hpp"
#include "wl/core/connection.hpp"
#include "wl/toplevel/workspaces.h"

#include <algorithm>
#include <cstdlib>

template <typename T, typename B>
void CompositorPlatform::wire_keyboard(std::unique_ptr<T>& storage, B& runtime) {
   
  storage = std::make_unique<T>(runtime);
  if (storage->ready()) {
    auto* raw = storage.get();
    cycle_layout_ = [raw] { return raw->nextLayout(); };
    layout_state_ = [raw] { return raw->currentLayout(); };
    current_layout_name_ = [raw] { return raw->activeLayoutName(); };
  }
}

template <typename T, typename B>
void CompositorPlatform::wire_output(std::unique_ptr<T>& storage, B& runtime,
                                     bool (*set_power)(B&, bool)) {
   
  storage = std::make_unique<T>(runtime);
  auto* raw = storage.get();
  focused_output_ = [raw] { return raw->focusedOutputName(); };
  set_output_power_ = [&runtime, set_power](bool on) { return set_power(runtime, on); };
}

CompositorPlatform::CompositorPlatform(eh::wayland::WaylandConnection& wl)
    : wl_(wl) {
   
  kind_ = detect_compositor_kind();
  create_backends();
}

CompositorPlatform::~CompositorPlatform() {
   
  cleanup();
}

void CompositorPlatform::initialize() {
   
  wl_.wayland_workspaces().initialize();
}

void CompositorPlatform::cleanup() {
   
  if (m_workspaceMetadataBackend != nullptr) {
    m_workspaceMetadataBackend->teardown();
    m_workspaceMetadataBackend.reset();
  }
  wl_.wayland_workspaces().teardown();
}

IWorkspaceManager* CompositorPlatform::workspace_backend() const {
   
  return wl_.wayland_workspaces().workspace_backend();
}

WaylandWorkspaces& CompositorPlatform::wayland_workspaces() const {
   
  return wl_.wayland_workspaces();
}

void CompositorPlatform::create_backends() {
   
  switch (kind_) {
    case CompositorKind::Niri:
      wire_keyboard<NiriKeyboardBackend>(niri_kb_, wl_.runtime_registry().niri());
      wire_output<NiriOutputBackend>(niri_out_, wl_.runtime_registry().niri(), niriSetOutputPower);
      m_workspaceMetadataBackend = std::make_unique<NiriWorkspaceManager>(wl_.runtime_registry().niri());
      break;
    case CompositorKind::Hyprland:
      wire_keyboard<HyprlandKeyboardBackend>(hyprland_kb_, wl_.runtime_registry().hyprland());
      wire_output<HyprlandOutputBackend>(hyprland_out_, wl_.runtime_registry().hyprland(),
                                         wspace::hyprland::setOutputPower);
      break;
    case CompositorKind::Sway:
      wire_keyboard<SwayKeyboardBackend>(sway_kb_, wl_.runtime_registry().sway());
      wire_output<SwayOutputBackend>(sway_out_, wl_.runtime_registry().sway(),
                                     wspace::sway::swaySetOutputPower);
      break;
    case CompositorKind::Triad:
      wire_keyboard<TriadKeyboardBackend>(triad_kb_, wl_.runtime_registry().triad());
      wire_output<TriadOutputBackend>(triad_out_, wl_.runtime_registry().triad(),
                                      wspace::triad::setMonitorPower);
      break;
    case CompositorKind::Mango:
      wire_keyboard<MangoKeyboardBackend>(mango_kb_, wl_.runtime_registry().mango());
      mango_out_ = std::make_unique<MangoOutputBackend>(wl_.runtime_registry().mango());
      focused_output_ = [raw = mango_out_.get()] { return raw->focusedOutputName(); };
      set_output_power_ = [this](bool on) { return wspace::mango::mangoSetOutputPower(wl_, on); };
      break;
    case CompositorKind::Labwc:
    case CompositorKind::Unknown:
      break;
  }
}

bool CompositorPlatform::cycle_keyboard_layout() const {
   
  return cycle_layout_ ? cycle_layout_() : false;
}

std::optional<LayoutInfo> CompositorPlatform::keyboard_layout_state() const {
   
  return layout_state_ ? layout_state_() : std::nullopt;
}

std::optional<std::string> CompositorPlatform::current_keyboard_layout_name() const {
   
  return current_layout_name_ ? current_layout_name_() : std::nullopt;
}

std::optional<std::string> CompositorPlatform::focused_output_name() const {
   
  return focused_output_ ? focused_output_() : std::nullopt;
}

bool CompositorPlatform::set_output_power(bool on) {
   
  return set_output_power_ ? set_output_power_(on) : false;
}

void CompositorPlatform::requestSessionExit() {
   
  switch (kind_) {
    case CompositorKind::Niri:
      (void)wl_.runtime_registry().niri().dispatchAction({{"Quit", {{"skip_confirmation", true}}}});
      break;
    case CompositorKind::Mango:
      std::system("mmsg dispatch quit >/dev/null 2>&1");
      break;
    default:
      break;
  }
}

wl_output* CompositorPlatform::preferredInteractiveOutput() const {
   
  switch (kind_) {
    case CompositorKind::Mango:
      return wl_.wayland_workspaces().mangoIpcSelectedOutput();
    default:
      return nullptr;
  }
}

std::optional<std::pair<std::string, std::string>>
CompositorPlatform::activeToplevel() const {
   
  switch (kind_) {
    case CompositorKind::Mango:
      return wl_.wayland_workspaces().mangoIpcFocusedClientOnOutput(preferredInteractiveOutput());
    default:
      return std::nullopt;
  }
}

void CompositorPlatform::setWorkspaceChangeCallback(ChangeCallback callback) {
   
  m_workspaceChangeCallback = std::move(callback);
  m_lastWorkspaceModelSnapshot = workspaceModelSnapshot();
  auto wrapper = [this]() {
    auto nextSnapshot = workspaceModelSnapshot();
    if (sameWorkspaceModelSnapshot(nextSnapshot, m_lastWorkspaceModelSnapshot)) {
      return;
    }
    m_lastWorkspaceModelSnapshot = std::move(nextSnapshot);
    if (m_workspaceChangeCallback) {
      m_workspaceChangeCallback();
    }
  };
  wl_.wayland_workspaces().onStateChange(wrapper);
  if (m_workspaceMetadataBackend != nullptr) {
    m_workspaceMetadataBackend->onStateChange(wrapper);
  }
}

void CompositorPlatform::onOverviewChange(ChangeCallback callback) {
   
  m_overviewChangeCallback = std::move(callback);
  if (m_workspaceMetadataBackend != nullptr) {
    m_workspaceMetadataBackend->onOverviewChange(m_overviewChangeCallback);
  }
}

void CompositorPlatform::activateWorkspace(const std::string& id) {
   
  wl_.wayland_workspaces().jumpTo(id);
}

void CompositorPlatform::activateWorkspace(wl_output* output, const std::string& id) {
   
  wl_.wayland_workspaces().jumpToOnOutput(output, id);
}

void CompositorPlatform::activateWorkspace(wl_output* output, const DeskRegion& workspace) {
   
  wl_.wayland_workspaces().jumpToOnOutput(output, workspace);
}

std::size_t CompositorPlatform::addWorkspacePollFds(std::vector<pollfd>& fds) const {
   
  const auto start = fds.size();
  auto& ww = wl_.wayland_workspaces();
  if (ww.eventFd() >= 0) {
    fds.emplace_back(ww.eventFd(), ww.eventFlags(), 0);
  }
  if (m_workspaceMetadataBackend != nullptr && m_workspaceMetadataBackend->eventFd() >= 0) {
    fds.emplace_back(m_workspaceMetadataBackend->eventFd(), m_workspaceMetadataBackend->eventFlags(), 0);
  }
  return start;
}

int CompositorPlatform::workspacePollTimeoutMs() const noexcept {
   
  int timeout = wl_.wayland_workspaces().eventTimeout();
  if (m_workspaceMetadataBackend != nullptr) {
    const int metaTimeout = m_workspaceMetadataBackend->eventTimeout();
    if (metaTimeout >= 0 && (timeout < 0 || metaTimeout < timeout)) {
      timeout = metaTimeout;
    }
  }
  return timeout;
}

void CompositorPlatform::dispatchWorkspacePoll(const std::vector<pollfd>& fds, std::size_t startIdx) {
   
  auto& ww = wl_.wayland_workspaces();
  std::size_t idx = startIdx;
  if (ww.eventFd() >= 0 && idx < fds.size() && fds[idx].fd == ww.eventFd()) {
    ww.onEvent(fds[idx].revents);
    ++idx;
  }
  if (m_workspaceMetadataBackend != nullptr && m_workspaceMetadataBackend->eventFd() >= 0 && idx < fds.size() && fds[idx].fd == m_workspaceMetadataBackend->eventFd()) {
    m_workspaceMetadataBackend->onEvent(fds[idx].revents);
  }
}

std::vector<DeskRegion> CompositorPlatform::workspaces(wl_output* output) const {
   
  auto ws = output != nullptr ? wl_.wayland_workspaces().regionsOnOutput(output) : wl_.wayland_workspaces().allRegions();
  if (m_workspaceMetadataBackend != nullptr) {
    m_workspaceMetadataBackend->sync(ws, connectorNameForOutput(output));
  }
  return ws;
}

std::unordered_map<std::string, std::vector<std::string>>
CompositorPlatform::appsByDesk(wl_output* outputFilter) const {
   
  if (m_workspaceMetadataBackend != nullptr) {
    const auto fromMeta = m_workspaceMetadataBackend->appsByDesk(connectorNameForOutput(outputFilter));
    if (!fromMeta.empty()) return fromMeta;
  }
  return wl_.wayland_workspaces().appsByDesk(outputFilter);
}

std::vector<std::string> CompositorPlatform::workspaceDisplayKeys(wl_output* outputFilter) const {
   
  if (m_workspaceMetadataBackend != nullptr) {
    return m_workspaceMetadataBackend->deskKeys(connectorNameForOutput(outputFilter));
  }
  return {};
}

std::vector<WorkspaceWindowAssignment>
CompositorPlatform::workspaceWindowAssignments(wl_output* outputFilter) const {
   
  std::vector<DeskWindow> windows;
  if (m_workspaceMetadataBackend != nullptr) {
    windows = m_workspaceMetadataBackend->windowsOnDesk(connectorNameForOutput(outputFilter));
  }
  if (windows.empty()) {
    windows = wl_.wayland_workspaces().windowsOnDesk(outputFilter);
  }
  std::vector<WorkspaceWindowAssignment> result;
  result.reserve(windows.size());
  for (const auto& w : windows) {
    result.push_back(WorkspaceWindowAssignment{
        .windowId = w.windowId,
        .workspaceKey = w.deskKey,
        .appId = w.appId,
        .title = w.title,
        .x = w.x,
        .y = w.y,
    });
  }
  return result;
}

TaskbarMode CompositorPlatform::taskbarMode() const noexcept {
   
  return wl_.wayland_workspaces().taskbarMode();
}

std::unordered_map<std::uintptr_t, DeskWindow>
CompositorPlatform::matchEntries(const std::vector<TaskbarEntry>& windows, wl_output* outputFilter) const {
   
  return wl_.wayland_workspaces().matchEntries(windows, outputFilter);
}

const char* CompositorPlatform::workspaceBackendName() const noexcept {
   
  return wl_.wayland_workspaces().name();
}

bool CompositorPlatform::tracksOverviewState() const noexcept {
   
  return m_workspaceMetadataBackend != nullptr && m_workspaceMetadataBackend->supportsOverview();
}

bool CompositorPlatform::hasOverview() const noexcept {
   
  return m_workspaceMetadataBackend != nullptr && m_workspaceMetadataBackend->hasOverview();
}

bool CompositorPlatform::overviewActive() const noexcept {
   
  return m_workspaceMetadataBackend == nullptr || m_workspaceMetadataBackend->overviewActive();
}

std::string CompositorPlatform::connectorNameForOutput(wl_output* output) const {
   
  if (output == nullptr) return {};
  return {};
}

std::vector<CompositorPlatform::WorkspaceModelSnapshot> CompositorPlatform::workspaceModelSnapshot() const {
   
  std::vector<WorkspaceModelSnapshot> snapshot;
  snapshot.emplace_back(0, workspaces(nullptr), workspaceWindowAssignments(nullptr));
  return snapshot;
}

bool CompositorPlatform::sameWorkspaceModelSnapshot(
    const std::vector<WorkspaceModelSnapshot>& lhs, const std::vector<WorkspaceModelSnapshot>& rhs) {
   
  auto sameWorkspace = [](const DeskRegion& a, const DeskRegion& b) {
    return a.id == b.id
        && a.name == b.name
        && a.coordinates == b.coordinates
        && a.active == b.active
        && a.urgent == b.urgent
        && a.occupied == b.occupied;
  };
  auto sameAssignment = [](const WorkspaceWindowAssignment& a, const WorkspaceWindowAssignment& b) {
    return a.windowId == b.windowId
        && a.workspaceKey == b.workspaceKey
        && a.appId == b.appId
        && a.title == b.title
        && a.x == b.x
        && a.y == b.y;
  };

  if (lhs.size() != rhs.size()) return false;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i].outputName != rhs[i].outputName
        || lhs[i].workspaces.size() != rhs[i].workspaces.size()
        || lhs[i].assignments.size() != rhs[i].assignments.size()) {
      return false;
    }
    for (std::size_t w = 0; w < lhs[i].workspaces.size(); ++w) {
      if (!sameWorkspace(lhs[i].workspaces[w], rhs[i].workspaces[w])) return false;
    }
    for (std::size_t a = 0; a < lhs[i].assignments.size(); ++a) {
      if (!sameAssignment(lhs[i].assignments[a], rhs[i].assignments[a])) return false;
    }
  }
  return true;
}
