#pragma once

#include <sys/types.h>

namespace eh::shell::notifications {

// Supervisor-side: spawn/kill the `horizon-notifications` child.
// The child owns the NotificationManager, the org.freedesktop.Notifications
// D-Bus service, and the toast host on its own Wayland connection.
// Returns the spawned child pid, or -1 on failure.
[[nodiscard]] pid_t notifications_spawn_native_child();
void notifications_kill_native_child(int pid);

}
