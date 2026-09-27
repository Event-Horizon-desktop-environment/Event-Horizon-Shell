#pragma once

namespace eh::shell::panel {

// Child entry for the split-out `horizon-panel` process: owns the top/bottom
// indicator bar + popups on its own WaylandConnection, tracks toplevels on
// that own connection (mirroring horizon-dock / horizon-taskbar), and runs
// the panel's timer/tray/inotify/mpris handlers on its own loop.
int run_panel_standalone();

}  // namespace eh::shell::panel
