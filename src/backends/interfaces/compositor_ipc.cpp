#include "backends/interfaces/compositor_ipc.h"

#include "backends/hyprland/hyprland_backends.h"
#include "backends/mango/mango_backends.h"
#include "backends/niri/niri_backends.h"
#include "backends/sway/sway_backends.h"

CompositorSessionRegistry::CompositorSessionRegistry()
    : m_niri(std::make_unique<NiriSession>())
    , m_hyprland(std::make_unique<wspace::hyprland::HyprlandSession>())
    , m_mango(std::make_unique<wspace::mango::MangoSession>())
    , m_sway(std::make_unique<wspace::sway::SwaySession>()) {
  MANGOWM_DEBUG("CompositorSessionRegistry ctor this=%p", (void*)this);
}

CompositorSessionRegistry::~CompositorSessionRegistry() {
  MANGOWM_DEBUG("CompositorSessionRegistry dtor this=%p", (void*)this);
}

NiriSession& CompositorSessionRegistry::niri() noexcept { return *m_niri; }

const NiriSession& CompositorSessionRegistry::niri() const noexcept { return *m_niri; }

wspace::hyprland::HyprlandSession& CompositorSessionRegistry::hyprland() noexcept { return *m_hyprland; }

const wspace::hyprland::HyprlandSession& CompositorSessionRegistry::hyprland() const noexcept { return *m_hyprland; }

wspace::mango::MangoSession& CompositorSessionRegistry::mango() noexcept { return *m_mango; }

const wspace::mango::MangoSession& CompositorSessionRegistry::mango() const noexcept { return *m_mango; }

wspace::sway::SwaySession& CompositorSessionRegistry::sway() noexcept { return *m_sway; }

const wspace::sway::SwaySession& CompositorSessionRegistry::sway() const noexcept { return *m_sway; }
