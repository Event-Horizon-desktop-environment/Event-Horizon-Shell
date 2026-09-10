#pragma once

#include <sys/types.h>

namespace eh::shell::dock {

// Supervisor-side: spawn/kill the split-out `horizon-dock` child. The child
// owns the dock + popups + launchpad/overview on its own WaylandConnection,
// timer fds, settings inotify, and MPRIS listener.
// Returns the spawned child pid, or -1 on failure.
[[nodiscard]] pid_t dock_spawn_native_child();
void dock_kill_native_child(int pid);

// Child-side pidfile helpers (used by run_dock_standalone so a stale child
// from a previous session can be reaped by the next spawn).
void dock_write_pid_file(pid_t p);
void dock_unlink_pid_file();

}
