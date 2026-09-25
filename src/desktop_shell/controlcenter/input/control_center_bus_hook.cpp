#include "desktop_shell/controlcenter/input/control_center_bus_hook.hpp"

#include <utility>

namespace eh::shell::dock::control_center {

namespace {
ControlCenterBusPublishFn g_fn;
}

void control_center_set_bus_publish_fn(ControlCenterBusPublishFn fn) { g_fn = std::move(fn); }

void control_center_bus_publish(const std::string& payload) {
  if (g_fn) g_fn(payload);
}

} // namespace eh::shell::dock::control_center
