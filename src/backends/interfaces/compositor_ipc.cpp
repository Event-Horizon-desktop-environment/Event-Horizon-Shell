#include "backends/interfaces/compositor_ipc.h"

#include "backends/hyprland/hyprland_backends.h"
#include "backends/mango/mango_backends.h"
#include "backends/niri/niri_backends.h"
#include "backends/sway/sway_backends.h"
#include "backends/triad/triad_backends.h"

CompositorRuntimeRegistry::CompositorRuntimeRegistry()
    : m_niri(std::make_unique<NiriRuntime>())
    , m_hyprland(std::make_unique<wspace::hyprland::HyprlandRuntime>())
    , m_mango(std::make_unique<wspace::mango::MangoRuntime>())
    , m_sway(std::make_unique<wspace::sway::SwayRuntime>())
    , m_triad(std::make_unique<wspace::triad::TriadRuntime>()) {
  MANGOWM_DEBUG("CompositorRuntimeRegistry ctor this=%p", (void*)this);
}

CompositorRuntimeRegistry::~CompositorRuntimeRegistry() {
  MANGOWM_DEBUG("CompositorRuntimeRegistry dtor this=%p", (void*)this);
}

NiriRuntime& CompositorRuntimeRegistry::niri() noexcept { return *m_niri; }

const NiriRuntime& CompositorRuntimeRegistry::niri() const noexcept { return *m_niri; }

wspace::hyprland::HyprlandRuntime& CompositorRuntimeRegistry::hyprland() noexcept { return *m_hyprland; }

const wspace::hyprland::HyprlandRuntime& CompositorRuntimeRegistry::hyprland() const noexcept { return *m_hyprland; }

wspace::mango::MangoRuntime& CompositorRuntimeRegistry::mango() noexcept { return *m_mango; }

const wspace::mango::MangoRuntime& CompositorRuntimeRegistry::mango() const noexcept { return *m_mango; }

wspace::sway::SwayRuntime& CompositorRuntimeRegistry::sway() noexcept { return *m_sway; }

const wspace::sway::SwayRuntime& CompositorRuntimeRegistry::sway() const noexcept { return *m_sway; }

wspace::triad::TriadRuntime& CompositorRuntimeRegistry::triad() noexcept { return *m_triad; }

const wspace::triad::TriadRuntime& CompositorRuntimeRegistry::triad() const noexcept { return *m_triad; }
