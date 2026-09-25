#pragma once

// Child -> supervisor command publish hook for the Control Centre popup.
// Mirrors the taskbar nightlight pattern (taskbar_set_nightlight_toggle_fn):
// horizon-dock / horizon-taskbar install it from their standalone IPC client;
// popup dispatch fires it; the supervisor self-client applies the command.
// Null-safe: fire functions no-op when no hook is installed (bare runs).

#include <functional>
#include <string>

namespace eh::shell::dock::control_center {

using ControlCenterBusPublishFn = std::function<void(const std::string& payload)>;

void control_center_set_bus_publish_fn(ControlCenterBusPublishFn fn);

// Publishes `command.request` <payload> when a hook is installed.
void control_center_bus_publish(const std::string& payload);

} // namespace eh::shell::dock::control_center
