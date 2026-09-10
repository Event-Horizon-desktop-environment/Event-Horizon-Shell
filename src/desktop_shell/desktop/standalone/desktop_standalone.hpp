#pragma once

namespace eh::shell::desktop {

// Entry point for the split-out `horizon-desktop` child. Owns
// a DesktopApp with its own WaylandConnection + IconCache, runs the desktop
// icon layer + widgets on their own timer fds, and re-applies config on the
// `config.applied` broadcast. The dock coupling that used to live here (DockApp*
// for output target, dock reserve, pin state, icon resolution, MPRIS) is now
// read from the shared config snapshot / the child's own MPRIS instance.
[[nodiscard]] int run_desktop_standalone();

}
