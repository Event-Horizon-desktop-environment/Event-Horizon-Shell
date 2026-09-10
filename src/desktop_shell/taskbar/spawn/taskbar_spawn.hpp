#pragma once

#include <sys/types.h>

namespace eh::shell::taskbar {

// Supervisor-side: spawn/kill the split-out `horizon-taskbar` child. The child
// owns the taskbar bar + app drawer + popups on its own WaylandConnection,
// timer fds, settings inotify, and MPRIS listener, and tracks toplevels on its
// own connection (mirroring `horizon-dock`).
// Returns the spawned child pid, or -1 on failure.
[[nodiscard]] pid_t taskbar_spawn_native_child();
void taskbar_kill_native_child(int pid);

// Child-side pidfile helpers (used by run_taskbar_standalone so a stale child
// from a previous session can be reaped by the next spawn).
void taskbar_write_pid_file(pid_t p);
void taskbar_unlink_pid_file();

}
