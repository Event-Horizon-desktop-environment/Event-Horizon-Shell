#pragma once

#include <sys/types.h>

namespace eh::shell::desktop {

// Supervisor-side: spawn/kill the split-out `horizon-desktop` child. The child
// owns the desktop icon layer + widgets on its own WaylandConnection, timer
// fds, and MPRIS listener.
// Returns the spawned child pid, or -1 on failure.
[[nodiscard]] pid_t desktop_spawn_native_child();
void desktop_kill_native_child(int pid);

// Child-side pidfile helpers (used by run_desktop_standalone so a stale child
// from a previous session can be reaped by the next spawn).
void desktop_write_pid_file(pid_t p);
void desktop_unlink_pid_file();

}
