#pragma once

namespace eh::shell::stage {

// Child entry for the split-out `horizon-stage` process: owns the overview
// (dashboard and OSD follow in later phases) on its own WaylandConnection
// plus the overview's second connection, with its own toplevel tracking,
// Super-key routing, timer/inotify/IPC handling on its own loop.
int run_stage_standalone();

}  // namespace eh::shell::stage
