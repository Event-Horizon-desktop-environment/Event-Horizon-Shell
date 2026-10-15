#pragma once

#if defined(EH_HAVE_POLKIT_AGENT)

namespace eh::polkit {

// Entry point for the split-out `horizon-polkit-agent` child. Registers as the
// session's org.freedesktop.PolicyKit1.AuthenticationAgent (libpolkit-agent
// re-registers on polkitd restarts) and owns the auth dialog + seat input on
// its own WaylandConnection, driven by its own PollMuxLoop with the service's
// GLib fds folded in. The supervisor only spawns/kills/respawns the process.
[[nodiscard]] int run_polkit_agent_standalone();

}

#endif
