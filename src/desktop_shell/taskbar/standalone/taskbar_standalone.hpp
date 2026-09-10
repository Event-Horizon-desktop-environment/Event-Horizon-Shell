#pragma once

namespace eh::shell::taskbar {

// Child entry for the split-out `horizon-taskbar` process: owns the taskbar
// bar + app drawer + popups on its own WaylandConnection, tracks toplevels on
// that own connection (mirroring `horizon-dock`), and runs the taskbar's
// timer/tray/inotify/mpris handlers on its own loop. Returns the process exit
// code.
int run_taskbar_standalone();

}
