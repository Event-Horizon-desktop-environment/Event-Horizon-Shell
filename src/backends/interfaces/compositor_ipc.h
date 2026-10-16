#pragma once

#include <memory>

class NiriSession;

namespace wspace::hyprland {
class HyprlandSession;
}

namespace wspace::mango {
class MangoSession;
}

namespace wspace::sway {
class SwaySession;
}

class CompositorSessionRegistry {
public:
  CompositorSessionRegistry();
  ~CompositorSessionRegistry();

  CompositorSessionRegistry(const CompositorSessionRegistry&) = delete;
  CompositorSessionRegistry& operator=(const CompositorSessionRegistry&) = delete;

  [[nodiscard]] NiriSession& niri() noexcept;
  [[nodiscard]] const NiriSession& niri() const noexcept;

  [[nodiscard]] wspace::hyprland::HyprlandSession& hyprland() noexcept;
  [[nodiscard]] const wspace::hyprland::HyprlandSession& hyprland() const noexcept;

  [[nodiscard]] wspace::mango::MangoSession& mango() noexcept;
  [[nodiscard]] const wspace::mango::MangoSession& mango() const noexcept;

  [[nodiscard]] wspace::sway::SwaySession& sway() noexcept;
  [[nodiscard]] const wspace::sway::SwaySession& sway() const noexcept;

private:
  std::unique_ptr<NiriSession> m_niri;
  std::unique_ptr<wspace::hyprland::HyprlandSession> m_hyprland;
  std::unique_ptr<wspace::mango::MangoSession> m_mango;
  std::unique_ptr<wspace::sway::SwaySession> m_sway;
};
