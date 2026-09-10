#include "backends/hyprland/hyprland_backends.h"

namespace wspace::hyprland {

HyprlandEventHandler::HyprlandEventHandler(HyprlandRuntime& rt) : m_rt(rt) {
  m_rt.registerEventHandler(this);
}

HyprlandEventHandler::~HyprlandEventHandler() { m_rt.unregisterEventHandler(this); }

} // namespace wspace::hyprland
