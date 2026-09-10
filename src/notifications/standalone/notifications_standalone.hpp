#pragma once

namespace eh::shell::notifications {

// Entry point for the split-out `horizon-notifications` child. Owns a
// WaylandConnection + NotificationManager + NotificationDbusService
// (org.freedesktop.Notifications) + NotificationToastHost on its own connection,
// subscribes to `notify.push`, and publishes `notify.changed`.
[[nodiscard]] int run_notifications_standalone();

}
