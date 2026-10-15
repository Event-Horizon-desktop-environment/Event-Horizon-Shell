#pragma once

#include <sys/types.h>

namespace eh::polkit {

// Supervisor-side: spawn/kill the `horizon-polkit-agent` child.
// The child registers as the session's polkit authentication agent and owns
// the auth dialog on its own Wayland connection. Mirrors the other split-out
// children (posix_spawn + setsid + log redirect); the SIGCHLD handler reaps
// and respawns it with the shared cooldown.
// Returns the spawned child pid, or -1 on failure.
[[nodiscard]] pid_t polkit_agent_spawn_native_child();
void polkit_agent_kill_native_child(int pid);

}  // namespace eh::polkit
