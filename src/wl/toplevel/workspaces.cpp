#include "wl/toplevel/workspaces.h"

#include "backends/interfaces/compositor_ipc.h"
#include "backends/ext_workspace/ext_workspace_manager.h"
#include "backends/hyprland/hyprland_workspace_manager.h"
#include "backends/mango/mango_workspace_manager.h"
#include "backends/sway/sway_workspace_manager.h"
#include "backends/triad/triad_workspace_manager.h"
#include "desktop_shell/unified/compositor_kind.hpp"

#include <string>

WaylandWorkspaces::WaylandWorkspaces(CompositorRuntimeRegistry& runtimeRegistry) {
  MANGOWM_DEBUG("WaylandWorkspaces ctor this=%p", (void*)this);
  auto extBackend = std::make_unique<ExtWorkspaceManager>();
  m_extWorkspaceBinder = extBackend.get();
  m_extBackend = extBackend.get();
  m_backends.push_back(std::move(extBackend));

  auto mangoIpcBackend = std::make_unique<MangoWorkspaceManager>(runtimeRegistry.mango());
  m_mangoIpcConnector = mangoIpcBackend.get();
  m_mangoIpcBackend = mangoIpcBackend.get();
  m_outputNameResolvers.push_back(mangoIpcBackend.get());
  m_outputObservers.push_back(mangoIpcBackend.get());
  m_backends.push_back(std::move(mangoIpcBackend));

  auto hyprlandBackend = std::make_unique<HyprlandWorkspaceManager>(runtimeRegistry.hyprland());
  m_hyprlandBackend = hyprlandBackend.get();
  m_outputNameResolvers.push_back(hyprlandBackend.get());
  m_backends.push_back(std::move(hyprlandBackend));

  auto swayBackend = std::make_unique<SwayWorkspaceManager>(runtimeRegistry.sway());
  m_swayBackend = swayBackend.get();
  m_outputNameResolvers.push_back(swayBackend.get());
  m_backends.push_back(std::move(swayBackend));

  auto triadBackend = std::make_unique<TriadWorkspaceManager>(runtimeRegistry.triad());
  m_triadBackend = triadBackend.get();
  m_backends.push_back(std::move(triadBackend));
}

WaylandWorkspaces::~WaylandWorkspaces() {
  MANGOWM_DEBUG("WaylandWorkspaces dtor this=%p", (void*)this);
}

void WaylandWorkspaces::bindExtProtocol(ext_workspace_manager_v1* manager) {
   
  if (m_extWorkspaceBinder != nullptr) {
    m_extWorkspaceBinder->bindExtProtocol(manager);
  }
}

void WaylandWorkspaces::setOutputResolver(std::function<std::string(wl_output*)> resolver) {
   
  for (auto* backend : m_outputNameResolvers) {
    if (backend != nullptr) {
      backend->setOutputResolver(resolver);
    }
  }
}

void WaylandWorkspaces::initialize() {
   
  const auto kind = detect_compositor_kind();

  if (kind == CompositorKind::Hyprland && m_hyprlandBackend != nullptr) {
    auto* hyprland = static_cast<HyprlandWorkspaceManager*>(m_hyprlandBackend);
    if (!hyprland->ready()) {
      hyprland->openConnection();
    }
    if (hyprland->ready()) {
      setActiveBackend(m_hyprlandBackend);
      return;
    }
  }

  if (kind == CompositorKind::Sway && m_swayBackend != nullptr && m_swayBackend->ready()) {
    setActiveBackend(m_swayBackend);
    return;
  }

  if (kind == CompositorKind::Triad && m_triadBackend != nullptr && m_triadBackend->ready()) {
    if (static_cast<TriadWorkspaceManager*>(m_triadBackend)->openConnection()) {
      setActiveBackend(m_triadBackend);
      return;
    }
  }

  if (kind == CompositorKind::Mango && m_mangoIpcConnector != nullptr) {
    if (m_mangoIpcConnector->openConnection() && m_mangoIpcBackend != nullptr &&
        m_mangoIpcBackend->ready()) {
      setActiveBackend(m_mangoIpcBackend);
      return;
    }
  }

  if (m_extBackend != nullptr && m_extBackend->ready()) {
    setActiveBackend(m_extBackend);
    return;
  }

  if (m_hyprlandBackend != nullptr) {
    auto* hyprland = static_cast<HyprlandWorkspaceManager*>(m_hyprlandBackend);
    if (!hyprland->ready()) {
      hyprland->openConnection();
    }
    if (hyprland->ready()) {
      setActiveBackend(m_hyprlandBackend);
      return;
    }
  }

  if (m_swayBackend != nullptr && m_swayBackend->ready()) {
    setActiveBackend(m_swayBackend);
    return;
  }

  if (m_triadBackend != nullptr && static_cast<TriadWorkspaceManager*>(m_triadBackend)->openConnection()) {
    setActiveBackend(m_triadBackend);
    return;
  }

  setActiveBackend(nullptr);
}

void WaylandWorkspaces::outputAttached(wl_output* output) {
   
  for (auto* backend : m_outputObservers) {
    if (backend != nullptr) {
      backend->outputAttached(output);
    }
  }
  if (m_activeBackend == m_hyprlandBackend && m_hyprlandBackend != nullptr) {
    static_cast<HyprlandWorkspaceManager*>(m_hyprlandBackend)->refreshSnapshot();
  }
}

void WaylandWorkspaces::outputDetached(wl_output* output) {
   
  for (auto* backend : m_outputObservers) {
    if (backend != nullptr) {
      backend->outputDetached(output);
    }
  }
}

void WaylandWorkspaces::onStateChange(ChangeCallback callback) {
  m_changeCallback = std::move(callback);
  auto wrapper = [this]() { notifyChanged(); };
  for (const auto& backend : m_backends) {
    if (backend != nullptr) {
      backend->onStateChange(wrapper);
    }
  }
}

void WaylandWorkspaces::jumpTo(const std::string& id) {
  if (m_activeBackend != nullptr) {
    m_activeBackend->jumpTo(id);
  }
}

void WaylandWorkspaces::jumpToOnOutput(wl_output* output, const std::string& id) {
  if (m_activeBackend != nullptr) {
    m_activeBackend->jumpToOnOutput(output, id);
  }
}

void WaylandWorkspaces::jumpToOnOutput(wl_output* output, const DeskRegion& workspace) {
  if (m_activeBackend != nullptr) {
    m_activeBackend->jumpToOnOutput(output, workspace);
  }
}

void WaylandWorkspaces::teardown() {
  for (const auto& backend : m_backends) {
    if (backend != nullptr) {
      backend->teardown();
    }
  }
  m_activeBackend = nullptr;
}

int WaylandWorkspaces::eventFd() const noexcept {
  return m_activeBackend != nullptr ? m_activeBackend->eventFd() : -1;
}

short WaylandWorkspaces::eventFlags() const noexcept {
  return m_activeBackend != nullptr ? m_activeBackend->eventFlags() : static_cast<short>(POLLIN);
}

int WaylandWorkspaces::eventTimeout() const noexcept {
  return m_activeBackend != nullptr ? m_activeBackend->eventTimeout() : -1;
}

void WaylandWorkspaces::onEvent(short revents) {
  if (m_activeBackend != nullptr) {
    m_activeBackend->onEvent(revents);
  }
}

const char* WaylandWorkspaces::name() const noexcept {
  return m_activeBackend != nullptr ? m_activeBackend->name() : "none";
}

std::unordered_map<std::string, std::vector<std::string>>
WaylandWorkspaces::appsByDesk(wl_output* output) const {
  return m_activeBackend != nullptr
             ? m_activeBackend->appsByDesk(output)
             : std::unordered_map<std::string, std::vector<std::string>>{};
}

TaskbarMode WaylandWorkspaces::taskbarMode() const noexcept {
  return m_activeBackend != nullptr ? m_activeBackend->taskbarMode() : TaskbarMode::Generic;
}

std::unordered_map<std::uintptr_t, DeskWindow>
WaylandWorkspaces::matchEntries(const std::vector<TaskbarEntry>& windows, wl_output* output) const {
  return m_activeBackend != nullptr
             ? m_activeBackend->matchEntries(windows, output)
             : std::unordered_map<std::uintptr_t, DeskWindow>{};
}

std::vector<DeskWindow> WaylandWorkspaces::windowsOnDesk(wl_output* output) const {
  return m_activeBackend != nullptr ? m_activeBackend->windowsOnDesk(output) : std::vector<DeskWindow>{};
}

void WaylandWorkspaces::bringToFront(const std::string& windowId) const {
  if (m_activeBackend != nullptr) {
    m_activeBackend->bringToFront(windowId);
  }
}

std::optional<std::string> WaylandWorkspaces::focusedWindowId() const {
  if (m_hyprlandBackend != nullptr && m_activeBackend == m_hyprlandBackend) {
    return static_cast<const HyprlandWorkspaceManager*>(m_hyprlandBackend)->focusedWindowId();
  }
  return std::nullopt;
}

std::vector<DeskRegion> WaylandWorkspaces::allRegions() const {
  return m_activeBackend != nullptr ? m_activeBackend->allRegions() : std::vector<DeskRegion>{};
}

std::vector<DeskRegion> WaylandWorkspaces::regionsOnOutput(wl_output* output) const {
  return m_activeBackend != nullptr ? m_activeBackend->regionsOnOutput(output) : std::vector<DeskRegion>{};
}

wl_output* WaylandWorkspaces::mangoIpcSelectedOutput() const {
  if (m_mangoIpcBackend == nullptr || !m_mangoIpcBackend->ready()) {
    return nullptr;
  }
  return static_cast<MangoWorkspaceManager*>(m_mangoIpcBackend)->ipcSelectedOutput();
}

std::optional<std::pair<std::string, std::string>>
WaylandWorkspaces::mangoIpcFocusedClientOnOutput(wl_output* output) const {
  if (m_mangoIpcBackend == nullptr || !m_mangoIpcBackend->ready()) {
    return std::nullopt;
  }
  return static_cast<const MangoWorkspaceManager*>(m_mangoIpcBackend)->ipcFocusedClientForOutput(output);
}

ISocketConnector* WaylandWorkspaces::mangoIpcConnector() const noexcept {
  return m_mangoIpcConnector;
}

void WaylandWorkspaces::setActiveBackend(IWorkspaceManager* backend) {
  m_activeBackend = backend;
}

void WaylandWorkspaces::notifyChanged() const {
  if (m_changeCallback) {
    m_changeCallback();
  }
}

HyprlandWorkspaceManager* WaylandWorkspaces::hyprlandBackend() const noexcept {
  if (m_hyprlandBackend != nullptr) {
    return static_cast<HyprlandWorkspaceManager*>(m_hyprlandBackend);
  }
  return nullptr;
}

namespace {
WaylandWorkspaces* g_wayland_workspaces = nullptr;
}

void set_global_wayland_workspaces(WaylandWorkspaces* ww) { g_wayland_workspaces = ww; }
WaylandWorkspaces* global_wayland_workspaces() { return g_wayland_workspaces; }
