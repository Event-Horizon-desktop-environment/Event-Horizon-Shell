#pragma once

namespace eh::shell::dock {

// Child entry for the split-out `horizon-dock` process: owns the dock + popups
// + launchpad/overview on its own WaylandConnection and runs the dock's
// timer/tray/inotify/mpris handlers on its own loop. Returns the process exit
// code.
int run_dock_standalone();

}
