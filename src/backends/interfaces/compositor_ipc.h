#pragma once

#include <memory>

class NiriRuntime;

namespace wspace::hyprland {
class HyprlandRuntime;
}

namespace wspace::mango {
class MangoRuntime;
}

namespace wspace::sway {
class SwayRuntime;
}

namespace wspace::triad {
class TriadRuntime;
}

class CompositorRuntimeRegistry {
public:
  CompositorRuntimeRegistry();
  ~CompositorRuntimeRegistry();

  CompositorRuntimeRegistry(const CompositorRuntimeRegistry&) = delete;
  CompositorRuntimeRegistry& operator=(const CompositorRuntimeRegistry&) = delete;

  [[nodiscard]] NiriRuntime& niri() noexcept;
  [[nodiscard]] const NiriRuntime& niri() const noexcept;

  [[nodiscard]] wspace::hyprland::HyprlandRuntime& hyprland() noexcept;
  [[nodiscard]] const wspace::hyprland::HyprlandRuntime& hyprland() const noexcept;

  [[nodiscard]] wspace::mango::MangoRuntime& mango() noexcept;
  [[nodiscard]] const wspace::mango::MangoRuntime& mango() const noexcept;

  [[nodiscard]] wspace::sway::SwayRuntime& sway() noexcept;
  [[nodiscard]] const wspace::sway::SwayRuntime& sway() const noexcept;

  [[nodiscard]] wspace::triad::TriadRuntime& triad() noexcept;
  [[nodiscard]] const wspace::triad::TriadRuntime& triad() const noexcept;

private:
  std::unique_ptr<NiriRuntime> m_niri;
  std::unique_ptr<wspace::hyprland::HyprlandRuntime> m_hyprland;
  std::unique_ptr<wspace::mango::MangoRuntime> m_mango;
  std::unique_ptr<wspace::sway::SwayRuntime> m_sway;
  std::unique_ptr<wspace::triad::TriadRuntime> m_triad;
};
