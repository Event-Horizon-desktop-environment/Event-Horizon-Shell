#include "wl/toplevel/workspaces.h"

#include "backends/interfaces/compositor_ipc.h"
#include "backends/ext_workspace/ext_workspace_manager.h"
#include "backends/hyprland/hyprland_workspace_manager.h"
#include "backends/mango/mango_workspace_manager.h"
#include "backends/sway/sway_workspace_manager.h"
#include "desktop_shell/unified/compositor_kind.hpp"

#include <string>

namespace {

CompositorWorkspaces* g_singleton = nullptr;

// True when the backend reports a live link; used by the selection ladder
// below so each rung reads as one condition instead of nested ifs.
bool linkUp(IDeskSource* backend) {
  return backend != nullptr && backend->ready();
}

} // namespace

void set_global_wayland_workspaces(CompositorWorkspaces* ww) { g_singleton = ww; }
CompositorWorkspaces* global_wayland_workspaces() { return g_singleton; }

CompositorWorkspaces::CompositorWorkspaces(CompositorSessionRegistry& runtimeRegistry) {
  MANGOWM_DEBUG("CompositorWorkspaces ctor this=%p", (void*)this);

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
}

CompositorWorkspaces::~CompositorWorkspaces() {
  MANGOWM_DEBUG("CompositorWorkspaces dtor this=%p", (void*)this);
}

// --- private helpers ------------------------------------------------------

void CompositorWorkspaces::setActiveBackend(IDeskSource* backend) {
  m_activeBackend = backend;
}

void CompositorWorkspaces::notifyChanged() const {
  if (m_changeCallback) {
    m_changeCallback();
  }
}

// --- construction / destruction ------------------------------------------

void CompositorWorkspaces::initialize() {

  const auto kind = detect_compositor_kind();

  // Selection ladder: kind-matched backends first (each opened on demand),
  // then whatever else is live. A rung only fires when the compositor kind
  // matches; the trailing fallbacks repeat the open-then-check dance for
  // environments where kind detection came back generic.
  if (kind == CompositorKind::Hyprland && m_hyprlandBackend != nullptr) {
    auto* hyprland = static_cast<HyprlandWorkspaceManager*>(m_hyprlandBackend);
    if (!hyprland->ready()) { hyprland->openConnection(); }
    if (hyprland->ready()) {
      setActiveBackend(m_hyprlandBackend);
      return;
    }
  }

  if (kind == CompositorKind::Sway && linkUp(m_swayBackend)) {
    setActiveBackend(m_swayBackend);
    return;
  }

  if (kind == CompositorKind::Mango && m_mangoIpcConnector != nullptr) {
    if (m_mangoIpcConnector->openConnection() && linkUp(m_mangoIpcBackend)) {
      setActiveBackend(m_mangoIpcBackend);
      return;
    }
  }

  if (linkUp(m_extBackend)) {
    setActiveBackend(m_extBackend);
    return;
  }

  if (m_hyprlandBackend != nullptr) {
    auto* hyprland = static_cast<HyprlandWorkspaceManager*>(m_hyprlandBackend);
    if (!hyprland->ready()) { hyprland->openConnection(); }
    if (hyprland->ready()) {
      setActiveBackend(m_hyprlandBackend);
      return;
    }
  }

  if (linkUp(m_swayBackend)) {
    setActiveBackend(m_swayBackend);
    return;
  }

  setActiveBackend(nullptr);
}

void CompositorWorkspaces::bindExtProtocol(ext_workspace_manager_v1* manager) {
  auto* binder = m_extWorkspaceBinder;
  if (binder != nullptr) { binder->bindExtProtocol(manager); }
}

void CompositorWorkspaces::setOutputResolver(std::function<std::string(wl_output*)> resolver) {

  callEach(m_outputNameResolvers, [&](auto& backend) { backend.setOutputResolver(resolver); });
}

void CompositorWorkspaces::onStateChange(DeskChangeCallback callback) {
  m_changeCallback = std::move(callback);
  auto wrapper = [this]() { notifyChanged(); };
  callEach(m_backends, [&](auto& backend) { backend.onStateChange(wrapper); });
}

void CompositorWorkspaces::teardown() {
  callEach(m_backends, [](auto& backend) { backend.teardown(); });
  m_activeBackend = nullptr;
}

// --- output life cycle ----------------------------------------------------

void CompositorWorkspaces::outputDetached(wl_output* output) {
  callEach(m_outputObservers, [&](auto& backend) { backend.outputDetached(output); });
}

void CompositorWorkspaces::outputAttached(wl_output* output) {
  callEach(m_outputObservers, [&](auto& backend) { backend.outputAttached(output); });
  auto* hyprland = m_hyprlandBackend;
  if (hyprland != nullptr && m_activeBackend == hyprland) {
    static_cast<HyprlandWorkspaceManager*>(hyprland)->reloadSnapshot();
  }
}

// --- navigation -----------------------------------------------------------

void CompositorWorkspaces::jumpToOnOutput(wl_output* output, const DeskSlot& workspace) {
  auto* backend = m_activeBackend;
  if (backend != nullptr) { backend->jumpToOnOutput(output, workspace); }
}

void CompositorWorkspaces::jumpToOnOutput(wl_output* output, const std::string& id) {
  auto* backend = m_activeBackend;
  if (backend != nullptr) { backend->jumpToOnOutput(output, id); }
}

void CompositorWorkspaces::jumpTo(const std::string& id) {
  auto* backend = m_activeBackend;
  if (backend != nullptr) { backend->jumpTo(id); }
}

// --- desk queries ---------------------------------------------------------

std::vector<DeskSlot> CompositorWorkspaces::regionsOnOutput(wl_output* output) const {
  auto* backend = m_activeBackend;
  if (backend == nullptr) { return {}; }
  return backend->regionsOnOutput(output);
}

std::vector<DeskSlot> CompositorWorkspaces::allRegions() const {
  auto* backend = m_activeBackend;
  if (backend == nullptr) { return {}; }
  return backend->allRegions();
}

CompositorWorkspaces::DeskAppIndex
CompositorWorkspaces::appsByDesk(wl_output* output) const {
  auto* backend = m_activeBackend;
  if (backend == nullptr) { return {}; }
  return backend->appsByDesk(output);
}

// --- bar / window integration ---------------------------------------------

std::vector<ToplevelRef> CompositorWorkspaces::windowsOnDesk(wl_output* output) const {
  auto* backend = m_activeBackend;
  if (backend == nullptr) { return {}; }
  return backend->windowsOnDesk(output);
}

CompositorWorkspaces::RowWindowIndex
CompositorWorkspaces::matchEntries(const std::vector<BarItem>& windows, wl_output* output) const {
  auto* backend = m_activeBackend;
  if (backend == nullptr) { return {}; }
  return backend->matchEntries(windows, output);
}

BarMatchPolicy CompositorWorkspaces::taskbarMode() const noexcept {
  auto* backend = m_activeBackend;
  if (backend == nullptr) { return BarMatchPolicy::Uniform; }
  return backend->taskbarMode();
}

void CompositorWorkspaces::bringToFront(const std::string& windowId) const {
  auto* backend = m_activeBackend;
  if (backend != nullptr) { backend->bringToFront(windowId); }
}

// --- polling --------------------------------------------------------------

void CompositorWorkspaces::onEvent(short revents) {
  auto* backend = m_activeBackend;
  if (backend != nullptr) { backend->onEvent(revents); }
}

int CompositorWorkspaces::eventTimeout() const noexcept {
  auto* backend = m_activeBackend;
  if (backend == nullptr) { return -1; }
  return backend->eventTimeout();
}

short CompositorWorkspaces::eventFlags() const noexcept {
  auto* backend = m_activeBackend;
  if (backend == nullptr) { return static_cast<short>(POLLIN); }
  return backend->eventFlags();
}

int CompositorWorkspaces::eventFd() const noexcept {
  auto* backend = m_activeBackend;
  if (backend == nullptr) { return -1; }
  return backend->eventFd();
}

// --- focus reporting ------------------------------------------------------

IDeskSource const* CompositorWorkspaces::readyMango() const {
  auto const* backend = m_mangoIpcBackend;
  return backend != nullptr && backend->ready() ? backend : nullptr;
}

CompositorWorkspaces::FocusedTitles
CompositorWorkspaces::mangoFocusedToplevel(wl_output* output) const {
  auto const* mango = static_cast<MangoWorkspaceManager const*>(readyMango());
  if (mango == nullptr) { return std::nullopt; }
  return mango->focusedToplevel(output);
}

wl_output* CompositorWorkspaces::mangoChosenOutput() const {
  auto const* mango = static_cast<MangoWorkspaceManager const*>(readyMango());
  return mango != nullptr ? mango->chosenOutput() : nullptr;
}

std::optional<std::string> CompositorWorkspaces::focusedWindowTitle() const {
  auto const* manager = static_cast<HyprlandWorkspaceManager const*>(m_hyprlandBackend);
  if (manager == nullptr || m_activeBackend != manager) { return std::nullopt; }
  return manager->focusedWindowTitle();
}

// --- owner accessors ------------------------------------------------------

HyprlandWorkspaceManager* CompositorWorkspaces::hyprlandBackend() const noexcept {
  auto* hyprland = m_hyprlandBackend;
  if (hyprland == nullptr) { return nullptr; }
  return static_cast<HyprlandWorkspaceManager*>(hyprland);
}

ILinkOpener* CompositorWorkspaces::mangoIpcConnector() const noexcept {
  return m_mangoIpcConnector;
}

const char* CompositorWorkspaces::name() const noexcept {
  auto* backend = m_activeBackend;
  if (backend == nullptr) { return "none"; }
  return backend->name();
}
