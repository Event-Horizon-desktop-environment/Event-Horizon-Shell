#include "backends/hyprland/hyprland_backends.h"

namespace wspace::hyprland {

HyprlandEventSink::HyprlandEventSink(HyprlandSession& rt) : m_rt(rt) {
  m_rt.addEventSink(this);
}

HyprlandEventSink::~HyprlandEventSink() { m_rt.removeEventSink(this); }

} // namespace wspace::hyprland
